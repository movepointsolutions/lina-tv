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

int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;

  GstElement *a_pipeline, *v_pipeline, *a_source, *v_source;
  GstElement *v_demux, *v_dec, *v_freeze, *a_sink, *v_sink;
  GstBus *bus;
  GstCaps *v_caps, *a_caps;
  guint bus_watch_id;

  /* Initialisation */
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);


  /*v_caps = gst_caps_new_simple ("video/x-raw",
		  "format", G_TYPE_STRING, "RGBA",
		  "width", G_TYPE_INT, 800,
		  "height", G_TYPE_INT, 1048,
		  "framerate", GST_TYPE_FRACTION, 25, 1,
		  NULL);
		  */
  a_caps = gst_caps_new_simple ("audio/x-raw",
		  "format", G_TYPE_STRING, "U8",
		  "channels", G_TYPE_INT, 2,
		  "rate", G_TYPE_INT, 48000,
		  NULL);

  /* Create gstreamer elements */
  a_pipeline = gst_pipeline_new ("audio-player");
  v_pipeline = gst_pipeline_new ("video-player");
  a_source = gst_element_factory_make ("filesrc",       "audio-source");
  v_source = gst_element_factory_make ("filesrc",       "video-source");
  v_demux  = gst_element_factory_make ("matroskademux", "video-demux");
  v_dec    = gst_element_factory_make ("vp9dec",        "video-decode");
  v_freeze = gst_element_factory_make ("imagefreeze",   "video-freeze");
  a_sink   = gst_element_factory_make ("autoaudiosink", "audio-output");
  v_sink   = gst_element_factory_make ("autovideosink", "video-output");

  if (!a_pipeline || !v_pipeline || !a_source || !v_source
		  || !v_demux || !v_dec || !v_freeze || !a_sink || !v_sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Set up the pipeline */

  /* we set the input filename to the source element */
  g_object_set (G_OBJECT (a_source), "location", "lina-tv.rgba", NULL);
  g_object_set (G_OBJECT (v_source), "location", "lina-tv.webm", NULL);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (a_pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* we add all elements into the pipeline */
  /* file-source | ogg-demuxer | vorbis-decoder | converter | alsa-output */
  gst_bin_add_many (GST_BIN (a_pipeline),
                    a_source, a_sink, NULL);
  gst_bin_add_many (GST_BIN (a_pipeline),
                    v_source, v_demux, v_dec, v_freeze, v_sink, NULL);

  //print_pad_capabilities(v_sink, "sink");

  /* we link the elements together */
  /* file-source -> ogg-demuxer ~> vorbis-decoder -> converter -> alsa-output */
  gst_element_link_filtered (a_source, a_sink, a_caps);
  g_signal_connect (v_demux, "pad-added", G_CALLBACK (on_pad_added), v_dec);
  gst_element_link_many (v_source, v_demux, NULL);
  gst_element_link_many (v_dec, v_sink, NULL);

  /* note that the demuxer will be linked to the decoder dynamically.
     The reason is that Ogg may contain various streams (for example
     audio and video). The source pad(s) will be created at run time,
     by the demuxer when it detects the amount and nature of streams.
     Therefore we connect a callback function which will be executed
     when the "pad-added" is emitted.*/


  /* Set the pipeline to "playing" state*/
  g_print ("Now playing.\n");
  gst_element_set_state (a_pipeline, GST_STATE_PLAYING);
  //gst_element_set_state (a_pipeline, GST_STATE_PLAYING);


  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);


  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (a_pipeline, GST_STATE_NULL);
  gst_element_set_state (a_pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (a_pipeline));
  gst_object_unref (GST_OBJECT (v_pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}

