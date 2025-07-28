//
// build parser test
// gcc hid_parser.c -o hid_parser -DPARSER_TEST -Ipico-sdk/lib/tinyusb/src/ -I. -Wall
//

#ifndef PARSER_TEST
#include "bsp/board.h"
#else
#define CFG_TUSB_MCU OPT_MCU_LPC54XXX
#endif

#include "tusb.h"

#include "hid_parser.h"

//--------------------------------------------------------------------+
// Report Descriptor Parser
//--------------------------------------------------------------------+

uint8_t hid_parse_report_descriptor(hid_report_info_t* report_info_arr, uint8_t arr_count, uint8_t const* desc_report, uint16_t desc_len)
{
  // Report Item 6.2.2.2 USB HID 1.11
  union TU_ATTR_PACKED
  {
    uint8_t byte;
    struct TU_ATTR_PACKED
    {
        uint8_t size : 2;
        uint8_t type : 2;
        uint8_t tag  : 4;
    };
  } header;

  tu_memclr(report_info_arr, arr_count*sizeof(tuh_hid_report_info_t));

  uint8_t report_num = 0;
  hid_report_info_t* info = report_info_arr;

  // current parsed report count & size from descriptor
  uint16_t ri_global_usage_page = 0;
  int32_t ri_global_logical_min = 0;
  int32_t ri_global_logical_max = 0;
  int32_t ri_global_physical_min = 0;
  int32_t ri_global_physical_max = 0;
  uint8_t ri_report_count = 0;
  uint8_t ri_report_size = 0;
  uint8_t ri_report_usage_count = 0;

  uint8_t ri_collection_depth = 0;

  while(desc_len && report_num < arr_count)
  {
    header.byte = *desc_report++;
    desc_len--;

    uint8_t const tag  = header.tag;
    uint8_t const type = header.type;
    uint8_t const size = header.size;

    uint32_t data;
    uint32_t sdata;
    switch (size) {
    case 1: data = desc_report[0]; sdata = ((data & 0x80) ? 0xFFFFFF00 : 0 ) | data; break;
    case 2: data = (desc_report[1] << 8) | desc_report[0]; sdata = ((data & 0x8000) ? 0xFFFF0000 : 0 ) | data;  break;
    case 3: data = (desc_report[3] << 24) | (desc_report[2] << 16) | (desc_report[1] << 8) | desc_report[0]; sdata = data; break;
    default: data = 0; sdata = 0;
    }

    TU_LOG2("tag = %d, type = %d, size = %d, data = ", tag, type, size);
    for(uint32_t i = 0; i < size; i++) TU_LOG(3, "%02X ", desc_report[i]);
    TU_LOG2("\r\n");

    switch(type)
    {
      case RI_TYPE_MAIN:
        switch (tag)
        {
          case RI_MAIN_INPUT:
          case RI_MAIN_OUTPUT:
          case RI_MAIN_FEATURE:
            TU_LOG2("INPUT %d\n", data);
            uint16_t offset = (info->num_items == 0) ? 0 : (info->item[info->num_items - 1].bit_offset + info->item[info->num_items - 1].bit_size);
            if (ri_report_usage_count > ri_report_count) {
                // skip first uncounted reports
                info->num_items += ri_report_usage_count - ri_report_count;
            }
            for (int i = 0; i < ri_report_count; i++) {
                if (info->num_items + i < MAX_REPORT_ITEMS) {
                    info->item[info->num_items + i].bit_offset = offset;
                    info->item[info->num_items + i].bit_size = ri_report_size;
                    info->item[info->num_items + i].item_type = tag;
                    info->item[info->num_items + i].attributes.logical.min = ri_global_logical_min;
                    info->item[info->num_items + i].attributes.logical.max = ri_global_logical_max;
                    info->item[info->num_items + i].attributes.physical.min = ri_global_physical_min;
                    info->item[info->num_items + i].attributes.physical.max = ri_global_physical_max;
                    info->item[info->num_items + i].attributes.usage.page = ri_global_usage_page;
                    if (ri_report_usage_count != ri_report_count && ri_report_usage_count > 0) {
                        if (i >= ri_report_usage_count) {
                            info->item[info->num_items + i].attributes.usage = info->item[info->num_items + i - 1].attributes.usage;
                        }
                    }
                } else {
                    printf("%s: too much items!\n", __func__);
                }
                offset += ri_report_size;
            }
            info->num_items += ri_report_count;
            ri_report_usage_count = 0;
          break;

          case RI_MAIN_COLLECTION:
            ri_report_usage_count = 0;
            ri_report_count = 0;
            ri_collection_depth++;
          break;

          case RI_MAIN_COLLECTION_END:
            ri_collection_depth--;
            if (ri_collection_depth == 0)
            {
              info++;
              report_num++;
            }
          break;

          default: break;
        }
      break;

      case RI_TYPE_GLOBAL:
        switch(tag)
        {
          case RI_GLOBAL_USAGE_PAGE:
            // only take in account the "usage page" before REPORT ID
            if ( ri_collection_depth == 0 ) {
                info->usage_page = data;
            }
            ri_global_usage_page = data;
          break;

          case RI_GLOBAL_LOGICAL_MIN   :
            ri_global_logical_min = sdata;
          break;
          case RI_GLOBAL_LOGICAL_MAX   :
            ri_global_logical_max = sdata;
          break;
          case RI_GLOBAL_PHYSICAL_MIN  :
            ri_global_physical_min = sdata;
          break;
          case RI_GLOBAL_PHYSICAL_MAX  :
            ri_global_physical_max = sdata;
          break;

          case RI_GLOBAL_REPORT_ID:
            info->report_id = data;
          break;

          case RI_GLOBAL_REPORT_SIZE:
            ri_report_size = data;
          break;

          case RI_GLOBAL_REPORT_COUNT:
            ri_report_count = data;
          break;

          case RI_GLOBAL_UNIT_EXPONENT : break;
          case RI_GLOBAL_UNIT          : break;
          case RI_GLOBAL_PUSH          : break;
          case RI_GLOBAL_POP           : break;

          default: break;
        }
      break;

      case RI_TYPE_LOCAL:
        switch(tag)
        {
          case RI_LOCAL_USAGE:
            // only take in account the "usage" before starting REPORT ID
            if ( ri_collection_depth == 0 ) {
                info->usage = data;
            } else {
                TU_LOG2("USAGE %02X\n", data);
                if (ri_report_usage_count < MAX_REPORT_ITEMS) {
                    info->item[info->num_items + ri_report_usage_count].attributes.usage.usage = data;
                    ri_report_usage_count++;
                } else {
                    printf("%s: too much report items!\n", __func__);
                }
            }
          break;

          case RI_LOCAL_USAGE_MIN        : break;
          case RI_LOCAL_USAGE_MAX        : break;
          case RI_LOCAL_DESIGNATOR_INDEX : break;
          case RI_LOCAL_DESIGNATOR_MIN   : break;
          case RI_LOCAL_DESIGNATOR_MAX   : break;
          case RI_LOCAL_STRING_INDEX     : break;
          case RI_LOCAL_STRING_MIN       : break;
          case RI_LOCAL_STRING_MAX       : break;
          case RI_LOCAL_DELIMITER        : break;
          default: break;
        }
      break;

      // error
      default:
        TU_LOG2("%s: Unknown type %02X\n", __func__, type);
      break;
    }

    desc_report += size;
    desc_len    -= size;
  }

  for ( uint8_t i = 0; i < report_num; i++ )
  {
    info = report_info_arr+i;
    TU_LOG2("%u: id = %u, usage_page = %u, usage = %u\r\n", i, info->report_id, info->usage_page, info->usage);
  }

  //////
  for (int i = 0; i < info->num_items; i++) {
    TU_LOG2("type %02X\n", info->item[i].item_type);
    TU_LOG2("  offset %d\n", info->item[i].bit_offset);
    TU_LOG2("  size   %d\n", info->item[i].bit_size);
    TU_LOG2("  page   %04X\n", info->item[i].attributes.usage.page);
    TU_LOG2("  usage  %04X\n", info->item[i].attributes.usage.usage);
    TU_LOG2("  logical min %d\n", info->item[i].attributes.logical.min);
    TU_LOG2("  logical max %d\n", info->item[i].attributes.logical.max);
    TU_LOG2("  physical min %d\n", info->item[i].attributes.physical.min);
    TU_LOG2("  physical max %d\n", info->item[i].attributes.physical.max);
  }
  //////

  return report_num;
}

bool hid_parse_find_item_by_page(hid_report_info_t* report_info_arr, uint8_t type, uint16_t page, const hid_report_item_t **item)
{
    for (int i = 0; i < report_info_arr->num_items; i++) {
        if (report_info_arr->item[i].item_type == type &&
            report_info_arr->item[i].attributes.usage.page == page) {
            if (item) {
                *item = &report_info_arr->item[i];
            }
            return true;
        }
    }

    return false;
}

bool hid_parse_find_item_by_usage(hid_report_info_t* report_info_arr, uint8_t type, uint16_t usage, const hid_report_item_t **item)
{
    for (int i = 0; i < report_info_arr->num_items; i++) {
        if (report_info_arr->item[i].item_type == type &&
            report_info_arr->item[i].attributes.usage.usage == usage) {
            if (item) {
                *item = &report_info_arr->item[i];
            }
            return true;
        }
    }

    return false;
}

bool hid_parse_find_bit_item_by_page(hid_report_info_t* report_info_arr, uint8_t type, uint16_t page, uint8_t bit, const hid_report_item_t **item)
{
    for (int i = 0; i < report_info_arr->num_items; i++) {
        if (report_info_arr->item[i].item_type == type &&
            report_info_arr->item[i].attributes.usage.page == page) {
            if (item) {
                if (i + bit < report_info_arr->num_items &&
                   report_info_arr->item[i + bit].item_type == type &&
                   report_info_arr->item[i + bit].attributes.usage.page == page) {
                    *item = &report_info_arr->item[i + bit];
                } else {
                    return false;
                }
            }
            return true;
        }
    }

    return false;
}

bool hid_parse_get_item_value(const hid_report_item_t *item, const uint8_t *report, uint8_t len, int32_t *value)
{
    if (item == NULL || report == NULL) {
        return false;
    }

    uint8_t boffs = item->bit_offset & 0x07;
    uint8_t pos = 8 - boffs;
    uint8_t offs  = item->bit_offset >> 3;
    uint32_t mask = ~(0xFFFFFFFF << item->bit_size);

    int32_t val = report[offs++] >> boffs;

    while (item->bit_size > pos) {
        val |= (report[offs++] << pos);
        pos += 8;
    }

    val &= mask;

    if (item->attributes.logical.min < 0) {
        if (val & (1 << (item->bit_size - 1))) {
            val |= (0xFFFFFFFF << item->bit_size);
        }
    }

    *value = val;

    return true;
}

#ifdef PARSER_TEST

#if 1
//static const uint8_t hid_report_desc[] = {
//    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
//    0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
//    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
//    0x81, 0x06, 0xC0, 0xC0
//};
static const uint8_t hid_report_desc[] = {
0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x02,        // Usage (Mouse)
0xA1, 0x01,        // Collection (Application)
0x09, 0x01,        //   Usage (Pointer)
0xA1, 0x00,        //   Collection (Physical)
0x05, 0x09,        //     Usage Page (Button)
0x19, 0x01,        //     Usage Minimum (0x01)
0x29, 0x03,        //     Usage Maximum (0x03)
0x15, 0x00,        //     Logical Minimum (0)
0x25, 0x01,        //     Logical Maximum (1)
0x75, 0x01,        //     Report Size (1)
0x95, 0x03,        //     Report Count (3)
0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x75, 0x05,        //     Report Size (5)
0x95, 0x01,        //     Report Count (1)
0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
0x09, 0x30,        //     Usage (X)
0x09, 0x31,        //     Usage (Y)
0x09, 0x38,        //     Usage (Wheel)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x03,        //     Report Count (3)
0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0xC0,              // End Collection
};
#else
static const uint8_t hid_report_desc[] = {
0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x02,        // Usage (Mouse)
0xA1, 0x01,        // Collection (Application)
0x05, 0x09,        //   Usage Page (Button)
0x19, 0x01,        //   Usage Minimum (0x01)
0x29, 0x03,        //   Usage Maximum (0x03)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x95, 0x03,        //   Report Count (3)
0x75, 0x01,        //   Report Size (1)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x01,        //   Report Count (1)
0x75, 0x05,        //   Report Size (5)
0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
0x09, 0x01,        //   Usage (Pointer)
0xA1, 0x00,        //   Collection (Physical)
0x09, 0x30,        //     Usage (X)
0x09, 0x31,        //     Usage (Y)
0x15, 0x81,        //     Logical Minimum (-127)
0x25, 0x7F,        //     Logical Maximum (127)
0x75, 0x08,        //     Report Size (8)
0x95, 0x02,        //     Report Count (2)
0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              //   End Collection
0x09, 0x38,        //   Usage (Wheel)
0x15, 0x81,        //   Logical Minimum (-127)
0x25, 0x7F,        //   Logical Maximum (127)
0x75, 0x08,        //   Report Size (8)
0x95, 0x01,        //   Report Count (1)
0x81, 0x06,        //   Input (Data,Var,Rel,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              // End Collection
};
#endif

#define MAX_REPORT 4

int main(int argc, char *argv[])
{
    hid_report_info_t info_arr;

    uint8_t num = hid_parse_report_descriptor(&info_arr, MAX_REPORT, hid_report_desc, sizeof(hid_report_desc));

    printf("parsed %d report(s)\n", num);

    printf("report id  %02x\n", info_arr.report_id);
    printf("usage      %02x\n", info_arr.usage);
    printf("usage page %04x\n", info_arr.usage_page);

    for (int i = 0; i < info_arr.num_items; i++) {
        printf(" %d\n", i);
        printf("  type         %02x\n", info_arr.item[i].item_type);
        printf("  bit offset   %d\n", info_arr.item[i].bit_offset);
        printf("  bits size    %d\n", info_arr.item[i].bit_size);
        printf("  flags        %04x\n", info_arr.item[i].item_flags);
        printf("  usage        %04x\n", info_arr.item[i].attributes.usage.usage);
        printf("  usage page   %04x\n", info_arr.item[i].attributes.usage.page);
        printf("  unit type    %08x\n", info_arr.item[i].attributes.unit.type);
        printf("  unit exp     %02x\n", info_arr.item[i].attributes.unit.exponent);
        printf("  logical min  %d\n", info_arr.item[i].attributes.logical.min);
        printf("  logical max  %d\n", info_arr.item[i].attributes.logical.max);
        printf("  physical min %d\n", info_arr.item[i].attributes.physical.min);
        printf("  physical max %d\n", info_arr.item[i].attributes.physical.max);
    }

    return 0;
}
#endif
