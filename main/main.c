#include "iec1107.h"
#include "esp_log.h"

extern export_values_t* export_hdl;
extern const int export_params_size;

static void print_exported_fields()
{
  for (int i = 0; i < export_params_size; ++i)
  {
    ESP_LOGI("Field Tag", "Exported Val %s", export_hdl -> export_holder[i]);
  }
}

static void iec1107_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  switch(event_id)
  {
  case IEC1107_PROTOCOL_ERROR:
    ESP_LOGI("Event Handler", "IEC1107_PROTOCOL_ERROR");
    break;
  case IEC1107_START_MESSAGE_NOT_RECIEVED:
    ESP_LOGI("Event Handler", "IEC1107_START_MESSAGE_NOT_RECIEVED");
    break;
  case IEC1107_START_MESSAGE_NOT_SENDED:
    ESP_LOGI("Event Handler", "IEC1107_START_MESSAGE_NOT_SENDED");
    break;
  case IEC1107_START_MESSAGE_SENDED:
     ESP_LOGI("Event Handler", "IEC1107_START_MESSAGE_SENDED");
    break;
  case IEC1107_START_MESSAGE_RECEIVED:
    ESP_LOGI("Event Handler", "SIEC1107_START_MESSAGE_RECEIVED");
    break;
  case IEC1107_READOUT_MESSAGE_SENDED:
    ESP_LOGI("Event Handler", "IEC1107_READOUT_MESSAGE_SENDED");
    break;
  case IEC1107_READOUT_MESSAGE_NOT_RECEIVED:
    ESP_LOGI("Event Handler", "IEC1107_READOUT_MESSAGE_NOT_RECEIVED");
    break;
  case IEC1107_READOUT_MESSAGE_RECEIVED:
    ESP_LOGI("Event Handler", "IEC1107_READOUT_MESSAGE_RECEIVED");
    break;
  case IEC1107_FIELDS_UPDATED:
    print_exported_fields();
    ESP_LOGI("Event Handler", "IEC1107_FIELDS_UPDATED");
    break;

  default:
    break;
  }
}

void app_main()
{
  iec1107_parser_handle_t iec1107 = iec1107_parser_init(LOOP, 10000);

  iec1107_parser_add_handler(iec1107, iec1107_event_handler, NULL);

  iec1107_start(iec1107);
}
