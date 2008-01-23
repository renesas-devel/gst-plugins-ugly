/* GStreamer
 *
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * xingmux.c: Unit test for the xingmux element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/check/gstcheck.h>

#include <math.h>

#include "xingmux_testdata.h"

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1," "layer = (int) 3")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1," "layer = (int) 3")
    );

GstElement *
setup_xingmux ()
{
  GstElement *xingmux;

  GST_DEBUG ("setup_xingmux");
  xingmux = gst_check_setup_element ("xingmux");
  mysrcpad = gst_check_setup_src_pad (xingmux, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (xingmux, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return xingmux;
}

void
cleanup_xingmux (GstElement * xingmux)
{
  GST_DEBUG ("cleanup_xingmux");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (xingmux);
  gst_check_teardown_sink_pad (xingmux);
  gst_check_teardown_element (xingmux);
}

GST_START_TEST (test_xing_remux)
{
  GstElement *xingmux;
  GstBuffer *inbuffer;
  GList *it;
  guint8 *verify_data;

  xingmux = setup_xingmux ();

  inbuffer = gst_buffer_new_and_alloc (sizeof (test_xing));
  memcpy (GST_BUFFER_DATA (inbuffer), test_xing, sizeof (test_xing));

  gst_buffer_set_caps (inbuffer, GST_PAD_CAPS (mysrcpad));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);


  /* FIXME: why are the xingmux pads flushing? */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_flush_stop ()));

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  /* ... and puts a new buffer on the global list */
  fail_unless_equals_int (g_list_length (buffers), 93);

  verify_data = test_xing;
  for (it = buffers; it != NULL; it = it->next) {
    GstBuffer *outbuffer = (GstBuffer *) it->data;

    if (it == buffers) {
      gint j;

      /* Empty Xing header, should be the same as input data until the "Xing" marker
       * and zeroes afterwards. */
      fail_unless (memcmp (test_xing, GST_BUFFER_DATA (outbuffer), 25) == 0);
      for (j = 26; j < GST_BUFFER_SIZE (outbuffer); j++)
        fail_unless (GST_BUFFER_DATA (outbuffer)[j] == 0);
      verify_data += GST_BUFFER_SIZE (outbuffer);
    } else if (it->next != NULL) {
      /* Should contain the raw MP3 data without changes */
      fail_unless (memcmp (verify_data, GST_BUFFER_DATA (outbuffer),
              GST_BUFFER_SIZE (outbuffer)) == 0);
      verify_data += GST_BUFFER_SIZE (outbuffer);
    } else {
      /* Last buffer is the rewrite of the first buffer and should be exactly the same
       * as the old Xing header we had */
      fail_unless (memcmp (test_xing, GST_BUFFER_DATA (outbuffer),
              GST_BUFFER_SIZE (outbuffer)) == 0);
    }
  }

  /* cleanup */
  cleanup_xingmux (xingmux);
}

GST_END_TEST;

Suite *
xingmux_suite (void)
{
  Suite *s = suite_create ("xingmux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_xing_remux);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = xingmux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
