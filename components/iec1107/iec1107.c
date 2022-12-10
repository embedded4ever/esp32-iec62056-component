#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <freertos/timers.h>
#include "iec1107.h"
#include "config.h"

#define IEC1107_PARSER_RUNTIME_BUFFER_SIZE (1024)
#define IEC1107_PARSER_RING_BUFFER_SIZE (IEC1107_PARSER_RUNTIME_BUFFER_SIZE * 2)
#define IEC1107_UART_NUM UART_NUM_1
#define IEC1107_START_MESSAGE_BAUD (300)
#define IEC1107_READOUT_MESSAGE_BAUD (9600)

//Think twice before change this value
#define IEC1107_EVENT_LOOP_QUEUE_SIZE (35)

/*
 * @brief Define of NMEA Parser Event base
 *
 */
ESP_EVENT_DEFINE_BASE(IEC1107_EVENT);

static const char *IEC1107_TAG = "iec1107_parser";

/* @brief Software Timer to reading meter */
static TimerHandle_t iec1107_cycle_timer = NULL;

/* @brief FreeRTOS event group to signal when we need to make a start & readout request */
static EventGroupHandle_t s_iec1107_event_group;

/* @brief indicate flags event management */
static const int START_MESSAGE_SEND = BIT0;
static const int START_MESSAGE_SENDED = BIT1;
static const int READOUT_MESSAGE_SEND = BIT2;
static const int READOUT_MESSAGE_SENDED = BIT3;
static const int READOUT_MESSAGE_ENDED = BIT4;

typedef struct {
  uint8_t* buffer;                          ///< Run time buffer
  uint16_t timeout;                         ///< Periodic Timer
  reading_mode_t read_mode;
  uart_port_t uart_port;
  esp_event_loop_handle_t event_loop_hdl;
  TaskHandle_t tsk_hdl;
  QueueHandle_t ev_queue;
}iec1107_t;

export_values_t* export_hdl = NULL;

static export_values_t* export_val_init()
{
  export_values_t* hdl = calloc(1, sizeof(export_values_t));

  if (!hdl)
  {
    goto err_hdl;
  }

  hdl -> export_holder = calloc(1, sizeof(char*) * export_params_size);

  if (!hdl -> export_holder)
  {
    goto err_export_holder;
  }

  for (int i = 0; i < export_params_size; ++i)
  {
    /*
     * Ex :
     * Obis Code 96.77.0*1, Found Val 14-07-25,16:23,14-07-25,16:23
     * 50 byte for Found Val
     */
    hdl -> export_holder[i] = calloc(1,sizeof(char) * 50);
    if (!hdl -> export_holder[i])
    {
      goto err_export_holder_item;
    }
  }

  return hdl;

err_export_holder_item:
  for (int i = 0; i < export_params_size; ++i)
  {
    free(hdl -> export_holder[i]);
  }
err_export_holder:
  free(hdl -> export_holder);
err_hdl:
  free(hdl);

  return NULL;
}

static void export_val_deinit()
{
  for (int i = 0; i < export_params_size; ++i)
  {
    free(export_hdl -> export_holder[i]);
  }

  free(export_hdl -> export_holder);

  free(export_hdl);
}

static void iec1107_timer_cb(TimerHandle_t xTimer)
{
  ESP_LOGI(IEC1107_TAG, "Reading EM Starting Again..");

  /* Stop the timer */
  xTimerStop(xTimer, (TickType_t) 0);

  /*Attempt to send start message */
  xEventGroupSetBits(s_iec1107_event_group, START_MESSAGE_SEND);
}

static void iec1107_management_task(void* pvParameters)
{
  iec1107_t* iec1107 = (iec1107_t*)pvParameters;
  EventBits_t bit;
  for(;;)
  {
    bit = xEventGroupGetBits(s_iec1107_event_group);

    // Send Start Message
    if (bit & START_MESSAGE_SEND)
    {
      uart_set_baudrate(iec1107 -> uart_port, 300);

      static const unsigned char hello_world[] = {0x2F, 0x3F, 0x21, 0x0D, 0x0A};
      static const size_t size = sizeof(hello_world) / sizeof(hello_world[0]);

      uart_write_bytes(iec1107 -> uart_port, (const char*)hello_world, size);

      esp_err_t r;
      do {
       r = uart_wait_tx_done(iec1107 -> uart_port, 1000 / portTICK_PERIOD_MS);
       if (r == ESP_ERR_TIMEOUT)
       {
         esp_event_post_to(iec1107 -> event_loop_hdl, IEC1107_EVENT, IEC1107_START_MESSAGE_NOT_SENDED, NULL, 0, 100 / portTICK_PERIOD_MS);
       }
      } while(r != ESP_OK);

      xEventGroupClearBits(s_iec1107_event_group, START_MESSAGE_SEND);
      xEventGroupSetBits(s_iec1107_event_group, START_MESSAGE_SENDED);
      esp_event_post_to(iec1107 -> event_loop_hdl, IEC1107_EVENT, IEC1107_START_MESSAGE_SENDED, NULL, 0, 100 / portTICK_PERIOD_MS);
    }
    //Send Readout Message
    else if (bit & READOUT_MESSAGE_SEND)
    {
      static const unsigned char readout_message[] = {0x06, 0x30, 0x35, 0x30, 0x0D, 0x0A};
      static const size_t size = sizeof(readout_message) / sizeof(readout_message[0]);

      uart_write_bytes(iec1107 -> uart_port, (const char*)readout_message, size);

      esp_err_t r;
      do {
       r = uart_wait_tx_done(iec1107 -> uart_port, 1000 / portTICK_PERIOD_MS);
       if (r == ESP_ERR_TIMEOUT)
       {
         esp_event_post_to(iec1107 -> event_loop_hdl, IEC1107_EVENT, IEC1107_READOUT_MESSAGE_NOT_RECEIVED, NULL, 0, 100 / portTICK_PERIOD_MS);
       }
      } while(r != ESP_OK);

      /*
       * To do : Determine Protocol B or C before switching the baud rate.
       */
      uart_set_baudrate(iec1107 -> uart_port, IEC1107_READOUT_MESSAGE_BAUD);

      xEventGroupClearBits(s_iec1107_event_group, READOUT_MESSAGE_SEND);
      xEventGroupSetBits(s_iec1107_event_group, READOUT_MESSAGE_SENDED);

      esp_event_post_to(iec1107 -> event_loop_hdl, IEC1107_EVENT, IEC1107_READOUT_MESSAGE_SENDED, NULL, 0, 100 / portTICK_PERIOD_MS);

      // if one shot mode activated, delete task itself
      if (iec1107 -> read_mode == SHOT)
      {
        vTaskDelete(NULL);
      }
    }
    //if reading mode is SHOT, code will not reach this statement
    else if (bit & READOUT_MESSAGE_ENDED)
    {
      ESP_LOGI(IEC1107_TAG, "Readout Message Ended");
      xEventGroupClearBits(s_iec1107_event_group, READOUT_MESSAGE_ENDED);
    }

    vTaskDelay(60); //Added for feeding wdt.
  }

  vTaskDelete(NULL);
}

//To Do : Calculate CRC
static void export_line(esp_event_loop_handle_t hdl, const uint8_t* buffer)
{
  //I think 20 is enough. think define a macro for this.
  char obis_code[20] = {0};
  char* p = NULL;
  p = strstr((const char*)buffer, "(");
  int idx = 0;

  //Remove magic number
  if (buffer[1] == '!')
  {
    esp_event_post_to(hdl, IEC1107_EVENT, IEC1107_READOUT_MESSAGE_RECEIVED, NULL, 0, 100 / portTICK_PERIOD_MS);
    esp_event_post_to(hdl, IEC1107_EVENT, IEC1107_FIELDS_UPDATED, NULL, 0, 100 / portTICK_PERIOD_MS);
    xEventGroupClearBits(s_iec1107_event_group, READOUT_MESSAGE_SENDED);
    xEventGroupSetBits(s_iec1107_event_group, READOUT_MESSAGE_ENDED);
    xTimerStart( iec1107_cycle_timer, (TickType_t)0 );
    return; // Don't remove return exp for this statement.
  }

  //To Do : Check STX and also maybe ETX
  //Skip the first [STX]
  buffer++;

  //Extract obis code
  /*
   * Ex :
   * buffer : 32.7.0(228.60*V)
   * p : (228.60*V)
   * Until buffer != (
   * obis code : 32.7.0
   * buffer : (228.60*V)
   */
  while (*buffer != *p)
  {
   obis_code[idx] = *buffer++;
   idx++;
  }

  obis_code[idx] = '\0';

  //buffer : 228.60*V)
  buffer++;

  idx = 0;

  //Extract value
  //I think 50 is enough. think define a macro for this.
  char buf[50] = {0};
  while (*p++ != ')')
  {
    buf[idx] = *buffer++;
    idx++;
  }

  //Remove ')' char
  buf[idx - 1] = '\0';

  for (int export_list_idx = 0; export_list_idx < export_params_size; ++export_list_idx)
  {
    if (strcmp(obis_code, export_obis_code[export_list_idx]) == 0)
    {
      strcpy(export_hdl -> export_holder[export_list_idx], buf);
      //ESP_LOGI(IEC1107_TAG, "Obis Code %s, Found Val %s", obis_code, buf);
    }
  }
}

static void handle_uart_pattern(iec1107_t* iec1107)
{
  int pos = uart_pattern_pop_pos(iec1107 -> uart_port);
  if (pos != -1 )
  {
    int len = uart_read_bytes(iec1107 -> uart_port, iec1107 -> buffer, pos + 1, 100 / portTICK_PERIOD_MS);
    iec1107 -> buffer[len] = '\0';

    EventBits_t bit;
    bit = xEventGroupGetBits(s_iec1107_event_group);

    if (bit & START_MESSAGE_SENDED)
    {
      // Identification too short
      if (len < 6)
      {
        esp_event_post_to(iec1107 -> event_loop_hdl, IEC1107_EVENT, IEC1107_START_MESSAGE_NOT_RECIEVED, NULL, 0, 100 / portTICK_PERIOD_MS);
        xEventGroupClearBits(s_iec1107_event_group, START_MESSAGE_SENDED);
        return;
      }

      ESP_LOGI(IEC1107_TAG, "Identification %s", iec1107 -> buffer);

      xEventGroupClearBits(s_iec1107_event_group, START_MESSAGE_SENDED);
      xEventGroupSetBits(s_iec1107_event_group, READOUT_MESSAGE_SEND);
    }
    else if (bit & READOUT_MESSAGE_SENDED)
    {
      //ESP_LOGI(IEC1107_TAG, "Line %s",  iec1107 -> buffer);
      export_line(iec1107 -> event_loop_hdl, iec1107 ->  buffer);
    }
  }
  else
  {
    //uart_flush maybe ?
  }
}

static void iec1107_uart_event_task(void *pvParameters)
{
  iec1107_t *iec1107 = (iec1107_t *)pvParameters;
  uart_event_t event;

  for(;;)
  {
    if(xQueueReceive(iec1107 -> ev_queue, (void * )&event, (portTickType)portMAX_DELAY))
    {
      switch(event.type)
      {
      case UART_DATA:
        break;
      case UART_FIFO_OVF:
        ESP_LOGI(IEC1107_TAG, "hw fifo overflow");
        uart_flush_input(iec1107 -> uart_port);
        xQueueReset(iec1107 -> ev_queue);
        break;
      case UART_BUFFER_FULL:
        ESP_LOGI(IEC1107_TAG, "ring buffer full");
        uart_flush_input(iec1107 -> uart_port);
        xQueueReset(iec1107 -> ev_queue);
        break;
      case UART_BREAK:
        ESP_LOGW(IEC1107_TAG, "uart rx break");
        break;
      case UART_PARITY_ERR:
        ESP_LOGE(IEC1107_TAG, "uart parity error");
        break;
      case UART_FRAME_ERR:
        ESP_LOGE(IEC1107_TAG, "uart frame error");
        break;
      case UART_PATTERN_DET:
        handle_uart_pattern(iec1107);
        break;
      default:
        ESP_LOGI(IEC1107_TAG, "uart event type: %d", event.type);
        break;
      }
    }

    esp_event_loop_run(iec1107 -> event_loop_hdl, pdMS_TO_TICKS(50));
  }

  vTaskDelete(NULL);
}

iec1107_parser_handle_t iec1107_parser_init(reading_mode_t mode, uint16_t timeout)
{
  iec1107_t* iec1107 = calloc(1, sizeof(iec1107_t));
  if (!iec1107)
  {
    ESP_LOGE(IEC1107_TAG, "Calloc memory failed for iec1107 struct");
    goto err_struct;
  }

  iec1107 -> read_mode = mode;
  iec1107 -> timeout = timeout;

  iec1107_cycle_timer =  xTimerCreate( NULL, pdMS_TO_TICKS(iec1107 -> timeout), pdFALSE, ( void * ) 0, iec1107_timer_cb);


  iec1107 -> buffer = calloc(1, IEC1107_PARSER_RUNTIME_BUFFER_SIZE);
  if (!iec1107 -> buffer)
  {
    ESP_LOGE(IEC1107_TAG, "Calloc memory failed for iec1107 buffer");
    goto err_buffer;
  }

  iec1107 -> uart_port = IEC1107_UART_NUM;

  uart_config_t port_config = {
      .baud_rate = IEC1107_START_MESSAGE_BAUD,
      .data_bits = UART_DATA_7_BITS,
      .parity = UART_PARITY_EVEN,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  if (uart_param_config(iec1107 -> uart_port, &port_config))
  {
    ESP_LOGE(IEC1107_TAG, "IEC1107 Port Config Failed");
    goto err_port;
  }

  if (uart_set_pin(iec1107 -> uart_port, 4, 5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE))
  {
    ESP_LOGE(IEC1107_TAG, "IEC1107 Pin Config Failed");
    goto err_port;
  }

  if (uart_driver_install(iec1107 -> uart_port, IEC1107_PARSER_RING_BUFFER_SIZE, 0,
                          IEC1107_EVENT_LOOP_QUEUE_SIZE, &iec1107 -> ev_queue, 0))
  {
    ESP_LOGE(IEC1107_TAG, "IEC1107 Port Driver Failed");
    goto err_driver;
  }

  uart_enable_pattern_det_baud_intr(iec1107 -> uart_port, '\r', 1, 9, 0, 0);
  uart_pattern_queue_reset(iec1107 -> uart_port, IEC1107_EVENT_LOOP_QUEUE_SIZE);

  uart_flush(iec1107 -> uart_port);

  esp_event_loop_args_t args = {
      .queue_size = IEC1107_EVENT_LOOP_QUEUE_SIZE,
      .task_name = NULL,
  };

  if (esp_event_loop_create(&args, &iec1107 -> event_loop_hdl))
  {
    ESP_LOGE(IEC1107_TAG, "IEC1107 Event Loop Failed");
    goto err_event_loop;
  }

  BaseType_t err = xTaskCreate(
                              iec1107_uart_event_task,
                              "iec1107_parser",
                              2048,
                              iec1107,
                              12,
                              &iec1107->tsk_hdl);
  if (err != pdTRUE)
  {
    ESP_LOGE(IEC1107_TAG, "create IEC1107 Uart Event task failed");
    goto err_task_create;
  }

  export_hdl = export_val_init();
  ESP_LOGI(IEC1107_TAG, "IEC1107 Create Ok");

  return iec1107;

err_task_create:
err_event_loop:
err_driver:
  uart_driver_delete(iec1107 -> uart_port);
err_port:
err_buffer:
  free(iec1107 -> buffer);
err_struct:
  free(iec1107);

  return NULL;
}

void iec1107_start(iec1107_parser_handle_t hdl)
{
  iec1107_t *iec1107 = (iec1107_t*)hdl;
  s_iec1107_event_group = xEventGroupCreate();

  BaseType_t err = xTaskCreate(
                              iec1107_management_task,
                              NULL,
                              2048,
                              iec1107,
                              12,
                              NULL);
  if (err != pdTRUE)
  {
    ESP_LOGE(IEC1107_TAG, "create IEC1107 Management task failed");
    return;
  }

  xEventGroupSetBits(s_iec1107_event_group, START_MESSAGE_SEND);
}


esp_err_t iec1107_parser_deinit(iec1107_parser_handle_t hdl)
{
  iec1107_t* iec1107 = (iec1107_t*)hdl;
  vTaskDelete(iec1107 -> tsk_hdl);
  esp_event_loop_delete(iec1107 -> event_loop_hdl);
  esp_err_t err = uart_driver_delete(iec1107 -> uart_port);
  free(iec1107 -> buffer);
  free(iec1107);

  export_val_deinit();

  return err;
}

esp_err_t iec1107_parser_add_handler(iec1107_parser_handle_t hdl, esp_event_handler_t event_handler, void* handler_arg)
{
  iec1107_t* iec1107 = (iec1107_t*)hdl;

  return esp_event_handler_register_with(iec1107 -> event_loop_hdl, IEC1107_EVENT,
                                         ESP_EVENT_ANY_ID, event_handler, handler_arg);
}

esp_err_t iec1107_parser_remove_handler(iec1107_parser_handle_t hdl, esp_event_handler_t event_handler)
{
  iec1107_t* iec1107 = (iec1107_t*)hdl;

  return esp_event_handler_unregister_with(iec1107 -> event_loop_hdl, IEC1107_EVENT, ESP_EVENT_ANY_ID, event_handler);
}
