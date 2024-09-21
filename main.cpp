#include "hidapi.h"
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
// all of the hardware details were repurposed from
// https://gitlab.com/CalcProgrammer1/OpenRGB/-/blob/d45d2bf7fc2a5ff490e34e2bfd58cc58b3af5b89/Controllers/CorsairPeripheralController/CorsairPeripheralController.cpp

constexpr int CORSAIR_VID = 0x1B1C;
constexpr int CORSAIR_K70_RGB_MK2_SE_PID = 0x1B6B;
constexpr int CORSAIR_PERIPHERAL_PACKET_LENGTH = 65;

enum { CORSAIR_COMMAND_WRITE = 0x07, CORSAIR_COMMAND_READ = 0x0E, CORSAIR_COMMAND_STREAM = 0x7F };
enum { CORSAIR_LIGHTING_CONTROL_HARDWARE = 0x01, CORSAIR_LIGHTING_CONTROL_SOFTWARE = 0x02 };
enum {
  CORSAIR_PROPERTY_FIRMWARE_INFO = 0x01,
  CORSAIR_PROPERTY_SPECIAL_FUNCTION = 0x04,
  CORSAIR_PROPERTY_LIGHTING_CONTROL = 0x05,
  CORSAIR_PROPERTY_SUBMIT_KEYBOARD_COLOR_24 = 0x28,
};

constexpr int NA = 1 << 20;
constexpr int ZONE_SIZE = 116;
constexpr int MATRIX_HEIGHT = 7;
constexpr int matrix_width = 23;
static unsigned int matrix_map_k70_mk2[MATRIX_HEIGHT][matrix_width] = {
    {
        NA, NA, NA, 115, 107, 8, NA, NA, NA, NA, NA, 113, 114, NA, NA, NA, NA, NA, NA, 16, NA, NA, NA,
    },
    {0, NA, 10, 18, 28, 36, NA, 46, 55, 64, 74, NA, 84, 93, 102, 6, 15, 24, 33, 26, 35, 44, 53},
    {1, 11, 19, 29, 37, 47, 56, 65, 75, 85, 94, NA, 103, 7, 25, NA, 42, 51, 60, 62, 72, 82, 91},
    {2, NA, 12, 20, 30, 38, NA, 48, 57, 66, 76, 86, 95, 104, 70, 80, 34, 43, 52, 9, 17, 27, 100},
    {3, NA, 13, 21, 31, 39, NA, 49, 58, 67, 77, 87, 96, 105, 98, 112, NA, NA, NA, 45, 54, 63, NA},
    {4, 111, 22, 32, 40, 50, NA, 59, NA, 68, 78, 88, 97, 106, 61, NA, NA, 81, NA, 73, 83, 92, 109},
    {5, 14, 23, NA, NA, NA, NA, 41, NA, NA, NA, NA, 69, 79, 89, 71, 90, 99, 108, 101, NA, 110, NA}};

static unsigned int keys_k70_mk2[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x12, 0x14, 0x15, 0x18, 0x19,
                                      0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2A, 0x2B, 0x2C, 0x30, 0x31, 0x32, 0x33,
                                      0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x42, 0x43, 0x44, 0x45, 0x48, 73,   74,   75,   76,
                                      78,   79,   80,   81,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   96,   97,   98,   99,   100,  101,
                                      102,  103,  104,  105,  108,  109,  110,  111,  112,  113,  115,  116,  117,  120,  121,  122,  123,  124,  126,  127,
                                      128,  129,  132,  133,  134,  135,  136,  137,  139,  140,  141,  16,   114,  47,   59,   125};

static unsigned int key_mapping_k95_plat_ansi[] = {0x31, 0x3f, 0x41, 0x42, 0x51, 0x53, 0x55, 0x6f, 0x7e, 0x7f, 0x80, 0x81};

} // namespace

namespace {
bool run = true;
void handle_sigint(int sig) { run = false; }

struct RGBColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

class Keyboard {
public:
  hid_device* handle = nullptr;

  ~Keyboard() { hid_close(handle); }
  bool is_attached() { return handle != nullptr; }
  bool attach(int vendor_id, int product_id) {
    if (handle)
      return false;
    handle = hid_open(vendor_id, product_id, nullptr);
    if (!handle)
      return false;

    send_special_function_control();
    send_lighting_control();
    send_k70_lighting_control();
    return true;
  }

  void send_rgb(const std::vector<RGBColor>& colors) {
    unsigned char red[168];
    unsigned char green[168];
    unsigned char blue[168];
    unsigned char data_sz = 24;

    /*-----------------------------------------------------*\
    | Zero out buffers                                      |
    \*-----------------------------------------------------*/
    memset(red, 0x00, sizeof(red));
    memset(green, 0x00, sizeof(green));
    memset(blue, 0x00, sizeof(blue));

    /*-----------------------------------------------------*\
    | Copy red, green, and blue components into buffers     |
    \*-----------------------------------------------------*/
    for (std::size_t color_idx = 0; color_idx < colors.size(); color_idx++) {
      RGBColor color = colors[color_idx];
      red[keys_k70_mk2[color_idx]] = color.r;
      green[keys_k70_mk2[color_idx]] = color.g;
      blue[keys_k70_mk2[color_idx]] = color.b;
    }

    /*-----------------------------------------------------*\
    | Send red bytes                                        |
    \*-----------------------------------------------------*/
    send_stream_packet(1, 60, &red[0]);
    send_stream_packet(2, 60, &red[60]);
    send_stream_packet(3, data_sz, &red[120]);
    send_rgb_channel(1, 3, 1);

    /*-----------------------------------------------------*\
    | Send green bytes                                      |
    \*-----------------------------------------------------*/
    send_stream_packet(1, 60, &green[0]);
    send_stream_packet(2, 60, &green[60]);
    send_stream_packet(3, data_sz, &green[120]);
    send_rgb_channel(2, 3, 1);

    /*-----------------------------------------------------*\
    | Send blue bytes                                       |
    \*-----------------------------------------------------*/
    send_stream_packet(1, 60, &blue[0]);
    send_stream_packet(2, 60, &blue[60]);
    send_stream_packet(3, data_sz, &blue[120]);
    send_rgb_channel(3, 3, 2);
  }

private:
  void send_special_function_control() {
    unsigned char usb_buf[CORSAIR_PERIPHERAL_PACKET_LENGTH];

    /*-----------------------------------------------------*\
    | Zero out buffer                                       |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    /*-----------------------------------------------------*\
    | Set up Lighting Control packet                        |
    \*-----------------------------------------------------*/
    usb_buf[0x00] = 0x00;
    usb_buf[0x01] = CORSAIR_COMMAND_WRITE;
    usb_buf[0x02] = CORSAIR_PROPERTY_SPECIAL_FUNCTION;
    usb_buf[0x03] = CORSAIR_LIGHTING_CONTROL_SOFTWARE;

    /*-----------------------------------------------------*\
    | Send packet                                           |
    \*-----------------------------------------------------*/
    hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
  }

  void send_lighting_control() {
    unsigned char usb_buf[CORSAIR_PERIPHERAL_PACKET_LENGTH];

    /*-----------------------------------------------------*\
    | Zero out buffer                                       |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    /*-----------------------------------------------------*\
    | Set up Lighting Control packet                        |
    \*-----------------------------------------------------*/
    usb_buf[0x00] = 0x00;
    usb_buf[0x01] = CORSAIR_COMMAND_WRITE;
    usb_buf[0x02] = CORSAIR_PROPERTY_LIGHTING_CONTROL;
    usb_buf[0x03] = CORSAIR_LIGHTING_CONTROL_SOFTWARE;

    /*-----------------------------------------------------*\
    | Lighting control byte needs to be 3 for keyboards and |
    | headset stand, 1 for mice and mousepads               |
    \*-----------------------------------------------------*/
    usb_buf[0x05] = 0x03; // On K95 Platinum, this controls keyboard brightness

    /*-----------------------------------------------------*\
    | Send packet                                           |
    \*-----------------------------------------------------*/
    hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
  }

  void send_k70_lighting_control() {
    unsigned char usb_buf[CORSAIR_PERIPHERAL_PACKET_LENGTH];

    /*-----------------------------------------------------*\
    | Zero out buffer                                       |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    /*-----------------------------------------------------*\
    | Set up a packet                                       |
    \*-----------------------------------------------------*/
    usb_buf[0x00] = 0x00;
    usb_buf[0x01] = CORSAIR_COMMAND_WRITE;
    usb_buf[0x02] = CORSAIR_PROPERTY_LIGHTING_CONTROL;
    usb_buf[0x03] = 0x08;

    usb_buf[0x05] = 0x01;

    /*-----------------------------------------------------*\
    | Send packet                                           |
    \*-----------------------------------------------------*/
    hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    unsigned int* skipped_identifiers = key_mapping_k95_plat_ansi;
    int skipped_identifiers_count = sizeof(key_mapping_k95_plat_ansi) / sizeof(key_mapping_k95_plat_ansi[0]);

    unsigned int identifier = 0;
    for (int i = 0; i < 4; i++) {
      /*-----------------------------------------------------*\
      | Zero out buffer                                       |
      \*-----------------------------------------------------*/
      memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

      /*-----------------------------------------------------*\
      | Set up a packet - a sequence of 120 ids               |
      \*-----------------------------------------------------*/
      usb_buf[0x00] = 0x00;
      usb_buf[0x01] = CORSAIR_COMMAND_WRITE;
      usb_buf[0x02] = 0x40;
      usb_buf[0x03] = 0x1E;

      for (int j = 0; j < 30; j++) {
        for (int j = 0; j < skipped_identifiers_count; j++) {
          if (identifier == skipped_identifiers[j]) {
            identifier++;
          }
        }

        usb_buf[5 + 2 * j] = identifier++;
        usb_buf[5 + 2 * j + 1] = 0xC0;
      }

      /*-----------------------------------------------------*\
      | Send packet                                           |
      \*-----------------------------------------------------*/
      hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
    }
  }

  void send_stream_packet(unsigned char packet_id, unsigned char data_sz, unsigned char* data_ptr) {
    unsigned char usb_buf[CORSAIR_PERIPHERAL_PACKET_LENGTH];

    /*-----------------------------------------------------*\
    | Zero out buffer                                       |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    /*-----------------------------------------------------*\
    | Set up Stream packet                                  |
    \*-----------------------------------------------------*/
    usb_buf[0x00] = 0x00;
    usb_buf[0x01] = CORSAIR_COMMAND_STREAM;
    usb_buf[0x02] = packet_id;
    usb_buf[0x03] = data_sz;

    /*-----------------------------------------------------*\
    | Copy in data bytes                                    |
    \*-----------------------------------------------------*/
    memcpy(&usb_buf[0x05], data_ptr, data_sz);

    /*-----------------------------------------------------*\
    | Send packet                                           |
    \*-----------------------------------------------------*/
    hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
  }

  void send_rgb_channel(unsigned char color_channel, unsigned char packet_count, unsigned char finish_val) {
    unsigned char usb_buf[CORSAIR_PERIPHERAL_PACKET_LENGTH];

    /*-----------------------------------------------------*\
    | Zero out buffer                                       |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    /*-----------------------------------------------------*\
    | Set up Submit Keyboard 24-Bit Colors packet           |
    \*-----------------------------------------------------*/
    usb_buf[0x00] = 0x00;
    usb_buf[0x01] = CORSAIR_COMMAND_WRITE;
    usb_buf[0x02] = CORSAIR_PROPERTY_SUBMIT_KEYBOARD_COLOR_24;
    usb_buf[0x03] = color_channel;
    usb_buf[0x04] = packet_count;
    usb_buf[0x05] = finish_val;

    /*-----------------------------------------------------*\
    | Send packet                                           |
    \*-----------------------------------------------------*/
    hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
  }

  void read_firmware_info() {
    int actual;
    unsigned char usb_buf[CORSAIR_PERIPHERAL_PACKET_LENGTH];
    char offset = 0;

    /*-----------------------------------------------------*\
    | Zero out buffer                                       |
    \*-----------------------------------------------------*/
    memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

    /*-----------------------------------------------------*\
    | Set up Read Firmware Info packet                      |
    \*-----------------------------------------------------*/
    usb_buf[0x00] = 0x00;
    usb_buf[0x01] = CORSAIR_COMMAND_READ;
    usb_buf[0x02] = CORSAIR_PROPERTY_FIRMWARE_INFO;

    /*-----------------------------------------------------*\
    | Send packet and try reading it using an HID read      |
    | If that fails, repeat the send and read the reply as  |
    | a feature report.                                     |
    \*-----------------------------------------------------*/
    hid_write(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
    actual = hid_read_timeout(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH, 1000);

    if (actual == 0) {
      /*-------------------------------------------------*\
      | Zero out buffer                                   |
      \*-------------------------------------------------*/
      memset(usb_buf, 0x00, CORSAIR_PERIPHERAL_PACKET_LENGTH);

      /*-------------------------------------------------*\
      | Set up Read Firmware Info packet                  |
      \*-------------------------------------------------*/
      usb_buf[0x00] = 0x00;
      usb_buf[0x01] = CORSAIR_COMMAND_READ;
      usb_buf[0x02] = CORSAIR_PROPERTY_FIRMWARE_INFO;

      hid_send_feature_report(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
      actual = hid_get_feature_report(handle, usb_buf, CORSAIR_PERIPHERAL_PACKET_LENGTH);
      offset = 1;
    }

    // Print device type
    std::cout << "Device type: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(usb_buf[0x14 + offset]) << std::endl;

    // Print device information based on type
    switch (usb_buf[0x14 + offset]) {
    case 0xC0: {
      uint16_t pid = (usb_buf[0x0E] << 8) | usb_buf[0x0F];
      std::cout << "Device: Keyboard" << std::endl;
      std::cout << "Product ID: 0x" << std::hex << pid << std::endl;

      std::cout << "Logical layout: ";
      switch (pid) {
      case 0x1B2D:
        std::cout << "K95 Platinum";
        break;
      case 0x1B11:
        std::cout << "K95";
        break;
      case 0x1B3D:
        std::cout << "K55";
        break;
      case 0x1B38:
      case 0x1B49:
      case 0x1B6B:
      case 0x1B55:
        std::cout << "K70 MK2";
        break;
      case 0x1B4F:
        std::cout << "K68";
        break;
      default:
        std::cout << "Normal";
      }
      std::cout << std::endl;

      std::cout << "Physical layout: ";
      switch (usb_buf[0x17 + offset]) {
      case 0x01:
        std::cout << "ANSI";
        break;
      case 0x02:
        std::cout << "ISO";
        break;
      case 0x03:
        std::cout << "ABNT";
        break;
      case 0x04:
        std::cout << "JIS";
        break;
      case 0x05:
        std::cout << "DUBEOLSIK";
        break;
      default:
        std::cout << "ANSI";
      }
      std::cout << std::endl;
    } break;

    case 0xC1:
      std::cout << "Device: Mouse" << std::endl;
      break;

    case 0xC2: {
      uint16_t pid = (usb_buf[0x0F] << 8) | usb_buf[0x0E];
      std::cout << "Product ID: 0x" << std::hex << pid << std::endl;
      if (pid == 0x0A34) {
        std::cout << "Device: Headset Stand" << std::endl;
      } else {
        std::cout << "Device: Mousemat" << std::endl;
      }
    } break;

    default:
      std::cout << "Device: Unknown" << std::endl;
      break;
    }

    // Print firmware version
    if (usb_buf[0x14 + offset] != 0xC0 && usb_buf[0x14 + offset] != 0xC1 && usb_buf[0x14 + offset] != 0xC2) {
      std::cout << "Unknown device type. Firmware version not available." << std::endl;
    } else {
      std::cout << "Firmware version: " << static_cast<int>(usb_buf[0x09 + offset]) << "." << static_cast<int>(usb_buf[0x08 + offset]) << std::endl;
    }
  }
};

} // namespace

int main(int argc, char** argv) {
  signal(SIGINT, handle_sigint);

  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " <r> <g> <b>\n";
    return 1;
  }

  uint8_t input[3];
  for (int i = 1; i <= 3; ++i) {
    unsigned long temp = std::strtoul(argv[i], nullptr, 10);
    input[i - 1] = temp;
  }

  if (hid_init() != 0) {
    std::cerr << "Failed to initialize HID" << std::endl;
    return 1;
  }

  {
    Keyboard keyboard;
    while (run && !keyboard.is_attached()) {
      keyboard.attach(CORSAIR_VID, CORSAIR_K70_RGB_MK2_SE_PID);
    }

    if (!run)
      goto exit;

    std::vector<RGBColor> colors(ZONE_SIZE);
    for (auto& color : colors) {
      color.r = input[0];
      color.g = input[1];
      color.b = input[2];
    }

    keyboard.send_rgb(colors);
  }

exit:
  hid_exit();
  return 0;
}
