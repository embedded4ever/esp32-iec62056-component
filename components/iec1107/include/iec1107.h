#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"
#include "driver/uart.h"


/*
 * @brief Declare of IEC1107 Event base
 *
 */
ESP_EVENT_DECLARE_BASE(IEC1107_EVENT);

/*
 * @brief IEC1107 Reading Modes
 *
 */
typedef enum {
  SHOT,
  LOOP,
} reading_mode_t;

/*
 * @brief IEC1107 Parser Handle
 *
 */
typedef void *iec1107_parser_handle_t;

/*
 * @brief IEC1107 Exported Values
 *
 */
typedef struct {
  char** export_holder;
}export_values_t;


/*
 * @brief IEC1107 Parser Event ID
 *
 */
typedef enum {
  IEC1107_PROTOCOL_ERROR,

  IEC1107_START_MESSAGE_NOT_SENDED,
  IEC1107_START_MESSAGE_SENDED,
  IEC1107_START_MESSAGE_NOT_RECIEVED,
  IEC1107_START_MESSAGE_RECEIVED,

  IEC1107_READOUT_MESSAGE_SENDED,
  IEC1107_READOUT_MESSAGE_NOT_RECEIVED,
  IEC1107_READOUT_MESSAGE_RECEIVED,

  IEC1107_FIELDS_UPDATED,
} iec1107_event_id_t;

/*
 * @brief Init IEC1107 Parser
 *
 * @param mode reading mode periodic or one shot
 * @param timeout cycle for periodic reading
 * @return iec1107_parser_handle_t handle of iec1107
 */
iec1107_parser_handle_t iec1107_parser_init(reading_mode_t mode, uint16_t timeout);

/*
 * @brief Start reading
 *
 * @param hdl handle of IEC1107 Parser
 */
void iec1107_start(iec1107_parser_handle_t hdl);

/*
 * @brief Deinit IEC1107 Parser
 *
 * @param hdl handle of IEC1107 Parser
 * @return esp_err_t
 */
esp_err_t iec1107_parser_deinit(iec1107_parser_handle_t hdl);

/*
 * @brief Add user defined handler for IEC1107 Parser
 *
 * @param hdl handle of IEC1107 Parser
 * @param event_handler user defined event handler
 * @param handler_arg handler specific arguments
 * @return esp_err_t
 */
esp_err_t iec1107_parser_add_handler(iec1107_parser_handle_t hdl, esp_event_handler_t event_handler, void *handler_arg);

/*
 * @brief Remove user defined handler for IEC1107 Parser
 *
 * @param hdl handle of IEC1107 Parser
 * @param event-handler user defined event handler
 */
esp_err_t iec1107_parser_remove_handler(iec1107_parser_handle_t hdl, esp_event_handler_t event_handler);

#ifdef __cplusplus
}
#endif
