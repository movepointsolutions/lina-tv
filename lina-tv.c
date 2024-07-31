#include <gst/gst.h>
#include <gio/gio.h>
#include <glib.h>
#include "pic.h"

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static GstPadProbeReturn
cb_have_data (GstPad          *pad,
              GstPadProbeInfo *info,
              gpointer         user_data)
{
  gint x, y;
  GstMapInfo map;
  guint8 *ptr;
  GstBuffer *buffer;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  buffer = gst_buffer_make_writable (buffer);

  /* Making a buffer writable can fail (for example if it
   * cannot be copied and is used more than once)
   */
  if (buffer == NULL)
    return GST_PAD_PROBE_OK;

  if (gst_buffer_map (buffer, &map, GST_MAP_WRITE)) {
    int detect_silence = 1;
    ptr = (guint8 *) map.data;
    int slnc = 1, slncf = 1;
    //g_print("Buffer: %lu\n", map.size);
    const double coeff = 0.08715574275;
    guint8 acc = 0;
    for (int i = 0; i < map.size; ++i) {
	    if (ptr[i])
		    slnc = 0;
	    if (ptr[i] != 0xff)
		    slncf = 0;
	    acc += ptr[i] * coeff;
	    ptr[i] = acc;
    }
    if (detect_silence && (slnc || slncf)) {
	    GMainLoop *loop = (GMainLoop *) user_data;
	    g_print ("Silence\n");
	    g_main_loop_quit (loop);
    } else
	    g_print("Sound\n");
    gst_buffer_unmap (buffer, &map);
  }

  GST_PAD_PROBE_INFO_DATA (info) = buffer;

  return GST_PAD_PROBE_OK;
}

struct Context {
  GMainLoop *loop;
  GstElement *a_pipeline, *v_pipeline, *a_source;
  GstElement *i_source, *i_dec, *i_convert, *i_freeze;
  GstElement *a_sink, *v_sink;
  GstCaps *v_caps, *a_caps;
};

int init_ctx(struct Context *ctx)
{
  ctx->loop = g_main_loop_new (NULL, FALSE);
  ctx->v_caps = gst_caps_new_simple ("video/x-raw",
		  "format", G_TYPE_STRING, "RGBA",
		  "width", G_TYPE_INT, 800,
		  "height", G_TYPE_INT, 1048,
		  NULL);
  ctx->a_caps = gst_caps_new_simple ("audio/x-raw",
		  "format", G_TYPE_STRING, "S16BE",
		  "channels", G_TYPE_INT, 2,
		  "rate", G_TYPE_INT, 48000,
		  NULL);

  /* Create gstreamer elements */
  ctx->a_pipeline = gst_pipeline_new ("audio-player");
  ctx->v_pipeline = gst_pipeline_new ("video-player");
  ctx->a_source = gst_element_factory_make ("giostreamsrc",  "audio-source");
  ctx->i_source = gst_element_factory_make ("giostreamsrc",  "image-source");
  ctx->i_dec    = gst_element_factory_make ("pngdec",        "image-decode");
  ctx->i_convert = gst_element_factory_make ("videoconvert", "video-convert");
  ctx->i_freeze = gst_element_factory_make ("imagefreeze",   "video-freeze");
  ctx->a_sink   = gst_element_factory_make ("autoaudiosink", "audio-output");
  ctx->v_sink   = gst_element_factory_make ("autovideosink", "video-output");


  if (!ctx->a_pipeline || !ctx->v_pipeline || !ctx->a_source
		  || !ctx->i_source || !ctx->i_dec || !ctx->i_freeze
		  || !ctx->a_sink || !ctx->v_sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Set up the pipeline */

  /* we set the input filename to the source element */
  GMemoryInputStream *a_stream;
  a_stream = G_MEMORY_INPUT_STREAM (g_memory_input_stream_new_from_data (lina_tv_rgba, lina_tv_rgba_len,
          (GDestroyNotify) g_free));
  GMemoryInputStream *i_stream;
  i_stream = G_MEMORY_INPUT_STREAM (g_memory_input_stream_new_from_data (lina_tv_png, lina_tv_png_len,
          (GDestroyNotify) g_free));
  //g_object_set (G_OBJECT (ctx->a_source), "location", "lina-tv.rgba", NULL);
  g_object_set (G_OBJECT (ctx->a_source), "stream", a_stream, NULL);
  g_object_set (G_OBJECT (ctx->i_source), "stream", i_stream, NULL);
  //g_object_set (G_OBJECT (ctx->a_source), "blocksize", 4096, NULL);
  //g_object_set (G_OBJECT (v_source), "location", "lina-tv.webm", NULL);
  //g_object_set (G_OBJECT (ctx->i_source), "location", "lina-tv.png", NULL);

  /* we add all elements into the pipeline */
  /* file-source | ogg-demuxer | vorbis-decoder | converter | alsa-output */
  gst_bin_add_many (GST_BIN (ctx->a_pipeline),
                    ctx->a_source, ctx->a_sink, NULL);
  gst_bin_add_many (GST_BIN (ctx->v_pipeline),
                    ctx->i_source, ctx->i_dec, ctx->i_convert,
		    ctx->i_freeze, ctx->v_sink, NULL);
  //gst_bin_add_many (GST_BIN (v_pipeline),
  //                  v_source, v_demux, v_dec, v_sink, NULL);

  //print_pad_capabilities(v_sink, "sink");
  GstPad *pad = gst_element_get_static_pad (ctx->a_source, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) cb_have_data, ctx->loop, NULL);

  /* we link the elements together */
  /* file-source -> ogg-demuxer ~> vorbis-decoder -> converter -> alsa-output */
  gst_element_link_filtered (ctx->a_source, ctx->a_sink, ctx->a_caps);
  /*g_signal_connect (v_demux, "pad-added", G_CALLBACK (on_pad_added), v_dec);
  gst_element_link_many (v_source, v_demux, NULL);
  gst_element_link_many (v_dec, v_sink, NULL);*/
  gst_element_link_many (ctx->i_source, ctx->i_dec, ctx->i_convert,
		         ctx->i_freeze, ctx->v_sink, NULL);
  return 0;
}

int
main (int   argc,
      char *argv[])
{
  gst_init (&argc, &argv);

  struct Context ctx;
  if (init_ctx(&ctx))
	  return -1;

  GstBus *bus;
  guint bus_watch_id;
  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (ctx.a_pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, ctx.loop);
  gst_object_unref (bus);


  /* Set the pipeline to "playing" state*/
  g_print ("Now playing.\n");
  gst_element_set_state (ctx.a_pipeline, GST_STATE_PLAYING);
  gst_element_set_state (ctx.v_pipeline, GST_STATE_PLAYING);


  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (ctx.loop);


  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (ctx.a_pipeline, GST_STATE_NULL);
  gst_element_set_state (ctx.v_pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (ctx.a_pipeline));
  gst_object_unref (GST_OBJECT (ctx.v_pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (ctx.loop);

  return 0;
}

