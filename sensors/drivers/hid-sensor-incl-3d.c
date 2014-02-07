/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include "./hid-sensor-trigger.h"

#define HID_USAGE_SENSOR_INCLINOMETER_3D			0x200086
#define HID_USAGE_SENSOR_ORIENT_TILT_HEADING			0x20047E
#define HID_USAGE_SENSOR_ORIENT_TILT_HEADING_X			0x20047F
#define HID_USAGE_SENSOR_ORIENT_TILT_HEADING_Y			0x200480
#define HID_USAGE_SENSOR_ORIENT_TILT_HEADING_Z			0x200481


enum incl_3d_channel {
        CHANNEL_SCAN_INDEX_X,
        CHANNEL_SCAN_INDEX_Y,
        CHANNEL_SCAN_INDEX_Z,
        INCL_3D_CHANNEL_MAX,
};

struct incl_3d_state {
        struct hid_sensor_hub_callbacks callbacks;
        struct hid_sensor_common common_attributes;
        struct hid_sensor_hub_attribute_info incl[INCL_3D_CHANNEL_MAX];
        u32 incl_val[INCL_3D_CHANNEL_MAX];
};

static const u32 incl_3d_addresses[INCL_3D_CHANNEL_MAX] = {
        HID_USAGE_SENSOR_ORIENT_TILT_HEADING_X,
        HID_USAGE_SENSOR_ORIENT_TILT_HEADING_Y,
        HID_USAGE_SENSOR_ORIENT_TILT_HEADING_Z
};

/* Channel definitions */
static const struct iio_chan_spec incl_3d_channels[] = {
        {
                .type = IIO_INCLI,
                .modified = 1,
                .channel2 = IIO_MOD_X,
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
                BIT(IIO_CHAN_INFO_SCALE) |
                BIT(IIO_CHAN_INFO_SAMP_FREQ) |
                BIT(IIO_CHAN_INFO_HYSTERESIS),
                .scan_index = CHANNEL_SCAN_INDEX_X,
        }, {
                .type = IIO_INCLI,
                .modified = 1,
                .channel2 = IIO_MOD_Y,
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
                BIT(IIO_CHAN_INFO_SCALE) |
                BIT(IIO_CHAN_INFO_SAMP_FREQ) |
                BIT(IIO_CHAN_INFO_HYSTERESIS),
                .scan_index = CHANNEL_SCAN_INDEX_Y,
        }, {
                .type = IIO_INCLI,
                .modified = 1,
                .channel2 = IIO_MOD_Z,
                .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
                BIT(IIO_CHAN_INFO_SCALE) |
                BIT(IIO_CHAN_INFO_SAMP_FREQ) |
                BIT(IIO_CHAN_INFO_HYSTERESIS),
                .scan_index = CHANNEL_SCAN_INDEX_Z,
        }
};

/* Adjust channel real bits based on report descriptor */
static void incl_3d_adjust_channel_bit_mask(struct iio_chan_spec *channels,
                                                int channel, int size)
{
        channels[channel].scan_type.sign = 's';
        /* Real storage bits will change based on the report desc. */
        channels[channel].scan_type.realbits = size * 8;
        /* Maximum size of a sample to capture is u32 */
        channels[channel].scan_type.storagebits = sizeof(u32) * 8;
}

/* Channel read_raw handler */
static int incl_3d_read_raw(struct iio_dev *indio_dev,
                              struct iio_chan_spec const *chan,
                              int *val, int *val2,
                              long mask)
{
        struct incl_3d_state *incl_state = iio_priv(indio_dev);
        int report_id = -1;
        u32 address;
        int ret;
        int ret_type;

        *val = 0;
        *val2 = 0;
        switch (mask) {
        case 0:
                report_id = incl_state->incl[chan->scan_index].report_id;
                address = incl_3d_addresses[chan->scan_index];
                if (report_id >= 0)
                        *val = sensor_hub_input_attr_get_raw_value(
                                incl_state->common_attributes.hsdev,
                                HID_USAGE_SENSOR_INCLINOMETER_3D, address,
                                report_id);
                else {
                        *val = 0;
                        return -EINVAL;
                }
                ret_type = IIO_VAL_INT;
                break;
        case IIO_CHAN_INFO_SCALE:
                *val = incl_state->incl[CHANNEL_SCAN_INDEX_X].units;
                ret_type = IIO_VAL_INT;
                break;
        case IIO_CHAN_INFO_OFFSET:
                *val = hid_sensor_convert_exponent(
                        incl_state->incl[CHANNEL_SCAN_INDEX_X].unit_expo);
                ret_type = IIO_VAL_INT;
                break;
        case IIO_CHAN_INFO_SAMP_FREQ:
                ret = hid_sensor_read_samp_freq_value(
                        &incl_state->common_attributes, val, val2);
                ret_type = IIO_VAL_INT_PLUS_MICRO;
                break;
        case IIO_CHAN_INFO_HYSTERESIS:
                ret = hid_sensor_read_raw_hyst_value(
                        &incl_state->common_attributes, val, val2);
                ret_type = IIO_VAL_INT_PLUS_MICRO;
                break;
        default:
                ret_type = -EINVAL;
                break;
        }

        return ret_type;
}

/* Channel write_raw handler */
static int incl_3d_write_raw(struct iio_dev *indio_dev,
                               struct iio_chan_spec const *chan,
                               int val,
                               int val2,
                               long mask)
{
        struct incl_3d_state *incl_state = iio_priv(indio_dev);
        int ret = 0;

        switch (mask) {
        case IIO_CHAN_INFO_SAMP_FREQ:
                ret = hid_sensor_write_samp_freq_value(
                                &incl_state->common_attributes, val, val2);
                break;
        case IIO_CHAN_INFO_HYSTERESIS:
                ret = hid_sensor_write_raw_hyst_value(
                                &incl_state->common_attributes, val, val2);
                break;
        default:
                ret = -EINVAL;
        }

        return ret;
}

static const struct iio_info incl_3d_info = {
        .driver_module = THIS_MODULE,
        .read_raw = &incl_3d_read_raw,
        .write_raw = &incl_3d_write_raw,
};

/* Function to push data to buffer */
static void hid_sensor_push_data(struct iio_dev *indio_dev, u8 *data, int len)
{
        dev_dbg(&indio_dev->dev, "hid_sensor_push_data\n");
        iio_push_to_buffers(indio_dev, (u8 *)data);
}

/* Callback handler to send event after all samples are received and captured */
static int incl_3d_proc_event(struct hid_sensor_hub_device *hsdev,
                                unsigned usage_id,
                                void *priv)
{
        struct iio_dev *indio_dev = platform_get_drvdata(priv);
        struct incl_3d_state *incl_state = iio_priv(indio_dev);

        dev_dbg(&indio_dev->dev, "incl_3d_proc_event [%d]\n",
                                incl_state->common_attributes.data_ready);
        if (incl_state->common_attributes.data_ready)
                hid_sensor_push_data(indio_dev,
                                (u8 *)incl_state->incl_val,
                                sizeof(incl_state->incl_val));

        return 0;
}

/* Capture samples in local storage */
static int incl_3d_capture_sample(struct hid_sensor_hub_device *hsdev,
                                unsigned usage_id,
                                size_t raw_len, char *raw_data,
                                void *priv)
{
        struct iio_dev *indio_dev = platform_get_drvdata(priv);
        struct incl_3d_state *incl_state = iio_priv(indio_dev);
        int offset;
        int ret = -EINVAL;

        switch (usage_id) {
        case HID_USAGE_SENSOR_ORIENT_TILT_HEADING_X:
        case HID_USAGE_SENSOR_ORIENT_TILT_HEADING_Y:
        case HID_USAGE_SENSOR_ORIENT_TILT_HEADING_Z:
                offset = usage_id - HID_USAGE_SENSOR_ORIENT_TILT_HEADING_X;
                incl_state->incl_val[CHANNEL_SCAN_INDEX_X + offset] =
                                                *(u32 *)raw_data;
                ret = 0;
        break;
        default:
                break;
        }

        return ret;
}

/* Parse report which is specific to an usage id*/
static int incl_3d_parse_report(struct platform_device *pdev,
                                struct hid_sensor_hub_device *hsdev,
                                struct iio_chan_spec *channels,
                                unsigned usage_id,
                                struct incl_3d_state *st)
{
        int ret;
        int i;

        for (i = 0; i <= CHANNEL_SCAN_INDEX_Z; ++i) {
                ret = sensor_hub_input_get_attribute_info(hsdev,
                                HID_INPUT_REPORT,
                                usage_id,
                                HID_USAGE_SENSOR_ORIENT_TILT_HEADING_X + i,
                                &st->incl[CHANNEL_SCAN_INDEX_X + i]);
                if (ret < 0)
                        break;
                incl_3d_adjust_channel_bit_mask(channels,
                                CHANNEL_SCAN_INDEX_X + i,
                                st->incl[CHANNEL_SCAN_INDEX_X + i].size);
        }
        dev_dbg(&pdev->dev, "incl_3d %x:%x, %x:%x, %x:%x\n",
                        st->incl[0].index,
                        st->incl[0].report_id,
                        st->incl[1].index, st->incl[1].report_id,
                        st->incl[2].index, st->incl[2].report_id);

        return ret;
}

/* Function to initialize the processing for usage id */
static int hid_incl_3d_probe(struct platform_device *pdev)
{
        int ret = 0;
        static const char *name = "incl_3d";
        struct iio_dev *indio_dev;
        struct incl_3d_state *incl_state;
        struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
        struct iio_chan_spec *channels;

        indio_dev = devm_iio_device_alloc(&pdev->dev,
                                          sizeof(struct incl_3d_state));
        if (indio_dev == NULL)
                return -ENOMEM;

        platform_set_drvdata(pdev, indio_dev);

        incl_state = iio_priv(indio_dev);
        incl_state->common_attributes.hsdev = hsdev;
        incl_state->common_attributes.pdev = pdev;

        ret = hid_sensor_parse_common_attributes(hsdev,
                                        HID_USAGE_SENSOR_INCLINOMETER_3D,
                                        &incl_state->common_attributes);
        if (ret) {
                dev_err(&pdev->dev, "failed to setup common attributes\n");
                return ret;
        }

        channels = kmemdup(incl_3d_channels, sizeof(incl_3d_channels),
                           GFP_KERNEL);
        if (!channels) {
                dev_err(&pdev->dev, "failed to duplicate channels\n");
                return -ENOMEM;
        }

        ret = incl_3d_parse_report(pdev, hsdev, channels,
                                        HID_USAGE_SENSOR_INCLINOMETER_3D, incl_state);
        if (ret) {
                dev_err(&pdev->dev, "failed to setup attributes\n");
                goto error_free_dev_mem;
        }

        indio_dev->channels = channels;
        indio_dev->num_channels = ARRAY_SIZE(incl_3d_channels);
        indio_dev->dev.parent = &pdev->dev;
        indio_dev->info = &incl_3d_info;
        indio_dev->name = name;
        indio_dev->modes = INDIO_DIRECT_MODE;

        ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
                NULL, NULL);
        if (ret) {
                dev_err(&pdev->dev, "failed to initialize trigger buffer\n");
                goto error_free_dev_mem;
        }
        incl_state->common_attributes.data_ready = false;
        ret = hid_sensor_setup_trigger(indio_dev, name,
                                        &incl_state->common_attributes);
        if (ret < 0) {
                dev_err(&pdev->dev, "trigger setup failed\n");
                goto error_unreg_buffer_funcs;
        }

        ret = iio_device_register(indio_dev);
        if (ret) {
                dev_err(&pdev->dev, "device register failed\n");
                goto error_remove_trigger;
        }

        incl_state->callbacks.send_event = incl_3d_proc_event;
        incl_state->callbacks.capture_sample = incl_3d_capture_sample;
        incl_state->callbacks.pdev = pdev;
        ret = sensor_hub_register_callback(hsdev, HID_USAGE_SENSOR_INCLINOMETER_3D,
                                        &incl_state->callbacks);
        if (ret < 0) {
                dev_err(&pdev->dev, "callback reg failed\n");
                goto error_iio_unreg;
        }

        return ret;

error_iio_unreg:
        iio_device_unregister(indio_dev);
error_remove_trigger:
        hid_sensor_remove_trigger(indio_dev);
error_unreg_buffer_funcs:
        iio_triggered_buffer_cleanup(indio_dev);
error_free_dev_mem:
        kfree(indio_dev->channels);
        return ret;
}

/* Function to deinitialize the processing for usage id */
static int hid_incl_3d_remove(struct platform_device *pdev)
{
        struct hid_sensor_hub_device *hsdev = pdev->dev.platform_data;
        struct iio_dev *indio_dev = platform_get_drvdata(pdev);

        sensor_hub_remove_callback(hsdev, HID_USAGE_SENSOR_INCLINOMETER_3D);
        iio_device_unregister(indio_dev);
        hid_sensor_remove_trigger(indio_dev);
        iio_triggered_buffer_cleanup(indio_dev);
        kfree(indio_dev->channels);

        return 0;
}

static struct platform_device_id hid_incl_3d_ids[] = {
        {
                /* Format: HID-SENSOR-usage_id_in_hex_lowercase */
		.name = "HID-SENSOR-200086",
        },
        { /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, hid_incl_3d_ids);

static struct platform_driver hid_incl_3d_platform_driver = {
        .id_table = hid_incl_3d_ids,
        .driver = {
                .name   = KBUILD_MODNAME,
                .owner  = THIS_MODULE,
        },
        .probe          = hid_incl_3d_probe,
        .remove         = hid_incl_3d_remove,
};
module_platform_driver(hid_incl_3d_platform_driver);

MODULE_DESCRIPTION("HID Sensor Inclinometer 3D");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_LICENSE("GPL");
