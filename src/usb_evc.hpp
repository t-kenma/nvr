#ifndef __USB_EVC_HPP_
#define __USB_EVC_HPP_

constexpr char USB_RAW_GADGET_DRIVER_DEFAULT[] = "renesas_usbhs_udc";
constexpr char USB_RAW_GADGET_DEVICE_DEFAULT[] = "11c60000.usb";

constexpr auto BCD_USB = 0x0200U; // USB 2.0
//constexpr auto BCD_USB = 0x0101U; // USB 1.1

constexpr auto BCD_DEVICE = 0x0001U;
constexpr auto USB_VENDOR = 0xEFFFU; // EVC.
constexpr auto USB_PRODUCT = 0x0001U; // EVC

constexpr auto MAX_PACKET_SIZE_CONTROL = 64U; // 8 in original ME56PS2
constexpr auto MAX_PACKET_SIZE_BULK = 512U;

constexpr auto STRING_ID_MANUFACTURER = 1U;
constexpr auto STRING_ID_PRODUCT = 2U;
constexpr auto STRING_ID_SERIAL = 3U;

struct usb_packet_control {
    struct usb_raw_ep_io header;
    char data[MAX_PACKET_SIZE_CONTROL];
};

struct usb_packet_bulk {
    struct usb_raw_ep_io header;
    char data[MAX_PACKET_SIZE_BULK];
};

struct _usb_endpoint_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__ ((packed));

template<int N>
struct _usb_string_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    char16_t wData[N];
} __attribute__ ((packed));

struct usb_config_descriptors {
    struct usb_config_descriptor config;
    struct usb_interface_descriptor interface;
    struct _usb_endpoint_descriptor endpoint_bulk_out;
    struct _usb_endpoint_descriptor endpoint_bulk_in;
    struct _usb_endpoint_descriptor endpoint_bulk_out2;
    struct _usb_endpoint_descriptor endpoint_bulk_in2;
};

struct usb_device_descriptor me56ps2_device_descriptor = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = __constant_cpu_to_le16(BCD_USB),
    .bDeviceClass       = 0xFF,
    .bDeviceSubClass    = 0xFF,
    .bDeviceProtocol    = 0xFF,
    .bMaxPacketSize0    = MAX_PACKET_SIZE_CONTROL,
    .idVendor           = __constant_cpu_to_le16(USB_VENDOR),
    .idProduct          = __constant_cpu_to_le16(USB_PRODUCT),
    .bcdDevice          = __constant_cpu_to_le16(BCD_DEVICE),
    .iManufacturer      = 0,
    .iProduct           = 0,
    .iSerialNumber      = 0,
    .bNumConfigurations = 1,
};

struct usb_config_descriptors me56ps2_config_descriptors = {
    .config = {
        .bLength             = USB_DT_CONFIG_SIZE,
        .bDescriptorType     = USB_DT_CONFIG,
        .wTotalLength        = __cpu_to_le16(sizeof(me56ps2_config_descriptors)),
        .bNumInterfaces      = 1,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = 0xC0,
        //.bmAttributes        = USB_CONFIG_ATT_WAKEUP,
        .bMaxPower           = 0xFA, // 500mA
    },
    .interface = {
        .bLength             = USB_DT_INTERFACE_SIZE,
        .bDescriptorType     = USB_DT_INTERFACE,
        .bInterfaceNumber    = 0,
        .bAlternateSetting   = 0,
        .bNumEndpoints       = 2,
        .bInterfaceClass     = 0xff, // Vendor Specific class
        .bInterfaceSubClass  = 0xff,
        .bInterfaceProtocol  = 0xff,
        .iInterface          = 0,
    },
    .endpoint_bulk_out = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x01,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    },
    .endpoint_bulk_in = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x82,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    },
    .endpoint_bulk_out2 = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x03,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    },
    .endpoint_bulk_in2 = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x84,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    }
};

const struct _usb_string_descriptor<1> me56ps2_string_descriptor_0 = {
    .bLength = sizeof(me56ps2_string_descriptor_0),
    .bDescriptorType = USB_DT_STRING,
    .wData = {__constant_cpu_to_le16(0x0409)},
};

// FIXME: specify byte order
const struct _usb_string_descriptor<3> me56ps2_string_descriptor_1 = { // Manufacturer
    .bLength = sizeof(me56ps2_string_descriptor_1),
    .bDescriptorType = USB_DT_STRING,
    .wData = {u'-', u'-', u'-'},
    //.wData = {u'N', u'/', u'A'},
};

const struct _usb_string_descriptor<14> me56ps2_string_descriptor_2 = { // Product
    .bLength = sizeof(me56ps2_string_descriptor_2),
    .bDescriptorType = USB_DT_STRING,
    .wData = {u'-', u'-', u'-'},
    //.wData = {u'M', u'o', u'd', u'e', u'm', u' ', u'e', u'm', u'u', u'l', u'a', u't', u'o', u'r'},
};

const struct _usb_string_descriptor<3> me56ps2_string_descriptor_3 = { // Serial
    .bLength = sizeof(me56ps2_string_descriptor_3),
    .bDescriptorType = USB_DT_STRING,
    .wData = {u'-', u'-', u'-'},
    //.wData = {u'N', u'/', u'A'},
};

constexpr auto STRING_DESCRIPTORS_NUM = 4;
const void *me56ps2_string_descriptors[STRING_DESCRIPTORS_NUM] = {
    &me56ps2_string_descriptor_0,
    &me56ps2_string_descriptor_1,
    &me56ps2_string_descriptor_2,
    &me56ps2_string_descriptor_3,
};

#endif

