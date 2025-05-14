#ifndef __USB_EVC_HPP_
#define __USB_EVC_HPP_

constexpr char USB_RAW_GADGET_DRIVER_DEFAULT[] = "renesas_usbhs_udc";
constexpr char USB_RAW_GADGET_DEVICE_DEFAULT[] = "11c60000.usb";

#if 0

constexpr auto BCD_USB = 0x0110U; // USB 1.1
constexpr auto BCD_DEVICE = 0x0101U;
constexpr auto USB_VENDOR = 0x0590U; // Omron Corp.
constexpr auto USB_PRODUCT = 0x001aU; // ME56PS2

constexpr auto ENDPOINT_ADDR_BULK = 2U;
constexpr auto MAX_PACKET_SIZE_CONTROL = 64U; // 8 in original ME56PS2
constexpr auto MAX_PACKET_SIZE_BULK = 64U;

#else

//constexpr auto BCD_USB = 0x0110U; // USB 1.1
constexpr auto BCD_USB = 0x0200U; // USB 2.0

constexpr auto BCD_DEVICE = 0x0001U;

constexpr auto USB_VENDOR = 0xEFFFU; // EVC
constexpr auto USB_PRODUCT = 0x0001U; // EVC
//constexpr auto USB_VENDOR = 0x0590U; // Omron Corp.
//constexpr auto USB_PRODUCT = 0x001aU; // ME56PS2
//constexpr auto USB_VENDOR = 0xEFFEU; // Dummy
//constexpr auto USB_PRODUCT = 0x000FU; // Dummy

constexpr auto ENDPOINT_ADDR_BULK = 2U;
constexpr auto MAX_PACKET_SIZE_CONTROL = 64U; // 8 in original ME56PS2
//constexpr auto MAX_PACKET_SIZE_BULK = 64U;
constexpr auto MAX_PACKET_SIZE_BULK = 512U;

#endif

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
};

const struct _usb_string_descriptor<3> me56ps2_string_descriptor_2 = { // Product
    .bLength = sizeof(me56ps2_string_descriptor_2),
    .bDescriptorType = USB_DT_STRING,
    .wData = {u'-', u'-', u'-'},
};

const struct _usb_string_descriptor<3> me56ps2_string_descriptor_3 = { // Serial
    .bLength = sizeof(me56ps2_string_descriptor_3),
    .bDescriptorType = USB_DT_STRING,
    .wData = {u'-', u'-', u'-'},
};

constexpr auto STRING_DESCRIPTORS_NUM = 4;

#endif

