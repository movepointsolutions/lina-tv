#include <gst/gst.h>
#include <glib.h>


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


static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the vorbis-decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}


/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

/* Prints information about a Pad Template, including its Capabilities */
static void print_pad_templates_information (GstElementFactory * factory) {
  const GList *pads;
  GstStaticPadTemplate *padtemplate;

  g_print ("Pad Templates for %s:\n", gst_element_factory_get_longname (factory));
  if (!gst_element_factory_get_num_pad_templates (factory)) {
    g_print ("  none\n");
    return;
  }

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    padtemplate = pads->data;
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

    if (padtemplate->presence == GST_PAD_ALWAYS)
      g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST)
      g_print ("    Availability: On request\n");
    else
      g_print ("    Availability: UNKNOWN!!!\n");

    if (padtemplate->static_caps.string) {
      GstCaps *caps;
      g_print ("    Capabilities:\n");
      caps = gst_static_caps_get (&padtemplate->static_caps);
      print_caps (caps, "      ");
      gst_caps_unref (caps);

    }

    g_print ("\n");
  }
}

/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }

  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_current_caps (pad);
  if (!caps)
    caps = gst_pad_query_caps (pad, NULL);

  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
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
    int xor_bytes = 1;
    ptr = (guint8 *) map.data;
    int slnc = 1, slncf = 1;
    //g_print("Buffer: %lu\n", map.size);
    for (int i = 0; i < map.size; ++i) {
	    if (ptr[i])
		    slnc = 0;
	    if (ptr[i] != 0xff)
		    slncf = 0;
	    if (xor_bytes && i)
		    ptr[i - 1] = ptr[i - 1] ^ ptr[i];
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
  ctx->a_source = gst_element_factory_make ("filesrc",       "audio-source");
  ctx->i_source = gst_element_factory_make ("filesrc",       "image-source");
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
  g_object_set (G_OBJECT (ctx->a_source), "location", "lina-tv.rgba", NULL);
  g_object_set (G_OBJECT (ctx->a_source), "blocksize", 4096, NULL);
  //g_object_set (G_OBJECT (v_source), "location", "lina-tv.webm", NULL);
  g_object_set (G_OBJECT (ctx->i_source), "location", "lina-tv.png", NULL);

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

