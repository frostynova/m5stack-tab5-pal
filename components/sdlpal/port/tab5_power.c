#include "tab5_power.h"

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Keep the BSP's FatFs typedefs out of the SDLPal translation unit. */
extern esp_err_t bsp_i2c_init(void);
extern i2c_master_bus_handle_t bsp_i2c_get_handle(void);

#define INA226_ADDRESS             0x41
#define INA226_REG_CONFIG          0x00
#define INA226_REG_SHUNT_VOLTAGE   0x01
#define INA226_REG_BUS_VOLTAGE     0x02
#define INA226_CONFIG_AVG16_1100US 0x0527

#define TAB5_POWER_EXPANDER_ADDRESS 0x44
#define TAB5_POWER_EXPANDER_DIR     0x03
#define TAB5_POWER_EXPANDER_OUTPUT  0x05
#define TAB5_POWER_EXPANDER_HIGHZ   0x07
#define TAB5_POWER_EXPANDER_INPUT   0x0f
#define TAB5_POWER_CHG_EN_MASK      (1u << 7)
#define TAB5_POWER_CHG_STAT_MASK    (1u << 6)
#define TAB5_POWER_CHG_QC_EN_MASK   (1u << 5)

#define I2C_TIMEOUT_MS 50
#define UPDATE_INTERVAL_US 1000000LL

static const char *TAG = "tab5_power";
static i2c_master_dev_handle_t g_Ina226;
static i2c_master_dev_handle_t g_PowerExpander;
static TAB5POWERSTATUS g_Status;
static int64_t g_LastUpdate;
static BOOL g_Initialized;

static esp_err_t
PAL_Tab5PowerAddDevice(
   i2c_master_bus_handle_t bus,
   uint8_t address,
   i2c_master_dev_handle_t *device
)
{
   const i2c_device_config_t config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = address,
      .scl_speed_hz = 400000,
   };
   return i2c_master_bus_add_device(bus, &config, device);
}

static esp_err_t
PAL_Tab5PowerWriteRegister16(
   i2c_master_dev_handle_t device,
   uint8_t reg,
   uint16_t value
)
{
   const uint8_t data[] = { reg, (uint8_t)(value >> 8), (uint8_t)value };
   return i2c_master_transmit(device, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t
PAL_Tab5PowerReadRegister16(
   i2c_master_dev_handle_t device,
   uint8_t reg,
   uint16_t *value
)
{
   uint8_t data[2];
   esp_err_t result = i2c_master_transmit_receive(
      device, &reg, 1, data, sizeof(data), I2C_TIMEOUT_MS);
   if (result == ESP_OK)
   {
      *value = ((uint16_t)data[0] << 8) | data[1];
   }
   return result;
}

static esp_err_t
PAL_Tab5PowerReadRegister8(
   i2c_master_dev_handle_t device,
   uint8_t reg,
   uint8_t *value
)
{
   return i2c_master_transmit_receive(
      device, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

static esp_err_t
PAL_Tab5PowerWriteRegister8(
   i2c_master_dev_handle_t device,
   uint8_t reg,
   uint8_t value
)
{
   const uint8_t data[] = { reg, value };
   return i2c_master_transmit(device, data, sizeof(data), I2C_TIMEOUT_MS);
}

static esp_err_t
PAL_Tab5PowerEnableCharging(
   VOID
)
{
   uint8_t output;
   uint8_t direction;
   uint8_t highz;

   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerReadRegister8(g_PowerExpander,
                                 TAB5_POWER_EXPANDER_OUTPUT, &output),
      TAG, "read charge output");
   output &= ~(TAB5_POWER_CHG_EN_MASK | TAB5_POWER_CHG_QC_EN_MASK);
   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerWriteRegister8(g_PowerExpander,
                                  TAB5_POWER_EXPANDER_OUTPUT, output),
      TAG, "prepare charger outputs");

   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerReadRegister8(g_PowerExpander,
                                 TAB5_POWER_EXPANDER_DIR, &direction),
      TAG, "read charge direction");
   direction |= TAB5_POWER_CHG_EN_MASK | TAB5_POWER_CHG_QC_EN_MASK;
   direction &= ~TAB5_POWER_CHG_STAT_MASK;
   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerWriteRegister8(g_PowerExpander,
                                  TAB5_POWER_EXPANDER_DIR, direction),
      TAG, "configure charge pins");

   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerReadRegister8(g_PowerExpander,
                                 TAB5_POWER_EXPANDER_HIGHZ, &highz),
      TAG, "read charge output mode");
   highz &= ~(TAB5_POWER_CHG_EN_MASK | TAB5_POWER_CHG_QC_EN_MASK);
   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerWriteRegister8(g_PowerExpander,
                                  TAB5_POWER_EXPANDER_HIGHZ, highz),
      TAG, "enable charge output drive");

   /* Match the official UserDemo sequence: negotiate first, then charge. */
   vTaskDelay(pdMS_TO_TICKS(50));
   output |= TAB5_POWER_CHG_EN_MASK;
   ESP_RETURN_ON_ERROR(
      PAL_Tab5PowerWriteRegister8(g_PowerExpander,
                                  TAB5_POWER_EXPANDER_OUTPUT, output),
      TAG, "enable charger");

   ESP_LOGI(TAG,
            "charger enabled: OUT=0x%02x DIR=0x%02x HIGHZ=0x%02x",
            output, direction, highz);
   return ESP_OK;
}

static INT
PAL_Tab5PowerEstimatePercent(
   float voltage
)
{
   static const struct
   {
      float voltage;
      INT percent;
   } curve[] = {
      { 6.60f,   0 },
      { 7.00f,  10 },
      { 7.20f,  20 },
      { 7.40f,  35 },
      { 7.60f,  55 },
      { 7.80f,  75 },
      { 8.00f,  90 },
      { 8.20f, 100 },
   };

   if (voltage <= curve[0].voltage) return 0;
   if (voltage >= curve[sizeof(curve) / sizeof(curve[0]) - 1].voltage) return 100;

   for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i)
   {
      if (voltage <= curve[i].voltage)
      {
         const float fraction = (voltage - curve[i - 1].voltage) /
                                (curve[i].voltage - curve[i - 1].voltage);
         return curve[i - 1].percent +
                (INT)(fraction * (curve[i].percent - curve[i - 1].percent) + 0.5f);
      }
   }
   return 0;
}

VOID
PAL_Tab5PowerInit(
   VOID
)
{
   if (g_Initialized)
   {
      return;
   }
   g_Initialized = TRUE;

   if (bsp_i2c_init() != ESP_OK)
   {
      ESP_LOGE(TAG, "could not initialize shared I2C bus");
      return;
   }

   i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
   esp_err_t result = PAL_Tab5PowerAddDevice(bus, INA226_ADDRESS, &g_Ina226);
   if (result != ESP_OK)
   {
      ESP_LOGE(TAG, "could not add INA226 at 0x%02x: %s",
               INA226_ADDRESS, esp_err_to_name(result));
      g_Ina226 = NULL;
      return;
   }

   result = PAL_Tab5PowerWriteRegister16(
      g_Ina226, INA226_REG_CONFIG, INA226_CONFIG_AVG16_1100US);
   if (result != ESP_OK)
   {
      ESP_LOGE(TAG, "could not configure INA226: %s", esp_err_to_name(result));
      return;
   }

   result = PAL_Tab5PowerAddDevice(
      bus, TAB5_POWER_EXPANDER_ADDRESS, &g_PowerExpander);
   if (result != ESP_OK)
   {
      ESP_LOGW(TAG, "CHG_STAT expander unavailable: %s", esp_err_to_name(result));
      g_PowerExpander = NULL;
   }
   else
   {
      result = PAL_Tab5PowerEnableCharging();
      if (result != ESP_OK)
      {
         ESP_LOGW(TAG, "could not enable Tab5 charger: %s",
                  esp_err_to_name(result));
      }
   }

   ESP_LOGI(TAG, "INA226 initialized at 0x%02x", INA226_ADDRESS);
   g_LastUpdate = 0;
   PAL_Tab5PowerUpdate();
}

VOID
PAL_Tab5PowerUpdate(
   VOID
)
{
   const int64_t now = esp_timer_get_time();
   uint16_t bus_raw;
   uint16_t shunt_raw;
   uint8_t expander_input = 0xff;

   if (!g_Initialized)
   {
      PAL_Tab5PowerInit();
   }
   if (g_Ina226 == NULL ||
       (g_LastUpdate != 0 && now - g_LastUpdate < UPDATE_INTERVAL_US))
   {
      return;
   }
   g_LastUpdate = now;

   esp_err_t result = PAL_Tab5PowerReadRegister16(
      g_Ina226, INA226_REG_BUS_VOLTAGE, &bus_raw);
   if (result == ESP_OK)
   {
      result = PAL_Tab5PowerReadRegister16(
         g_Ina226, INA226_REG_SHUNT_VOLTAGE, &shunt_raw);
   }
   if (result != ESP_OK)
   {
      ESP_LOGW(TAG, "INA226 read failed: %s", esp_err_to_name(result));
      g_Status.valid = FALSE;
      return;
   }

   const float voltage = bus_raw * 0.00125f;
   const float measured_current = (int16_t)shunt_raw * 0.0005f;
   /* Present current from the battery user's perspective: +charge, -drain. */
   const float current = -measured_current;
   if (!g_Status.valid)
   {
      g_Status.voltage = voltage;
   }
   else
   {
      g_Status.voltage = g_Status.voltage * 0.8f + voltage * 0.2f;
   }
   g_Status.current = current;
   g_Status.percent = PAL_Tab5PowerEstimatePercent(g_Status.voltage);
   g_Status.valid = TRUE;

   if (g_PowerExpander != NULL &&
       PAL_Tab5PowerReadRegister8(g_PowerExpander,
                                  TAB5_POWER_EXPANDER_INPUT,
                                  &expander_input) == ESP_OK)
   {
      /* Keep CHG_STAT for diagnostics; signed shunt current is definitive. */
   }

   if (current >= 0.05f)
   {
      g_Status.state = kTab5PowerCharging;
   }
   else if (current <= -0.05f)
   {
      g_Status.state = kTab5PowerDischarging;
   }
   else
   {
      g_Status.state = kTab5PowerIdle;
   }

   static unsigned sample_count = 0;
   ++sample_count;
   if (sample_count == 1 || sample_count == 5)
   {
      ESP_LOGI(TAG, "battery %.3f V, current %+.3f A, CHG_STAT=%d, estimate=%d%%",
               g_Status.voltage, g_Status.current,
               (expander_input & TAB5_POWER_CHG_STAT_MASK) != 0,
               g_Status.percent);
   }
}

TAB5POWERSTATUS
PAL_Tab5PowerGetStatus(
   VOID
)
{
   return g_Status;
}
