#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#include "wifi.h"
#include "mqtt.h"
#include "dht11.h"
#include "ssd1306.h"

// Definição dos pinos
#define RELAY_PIN 2          // Pino para controle do relé (aquecimento)
#define BUTTON_PIN 3         // Pino para botão de acionamento manual
#define DHT_PIN 4            // Pino do sensor DHT11
#define SDA_GPIO 21          // Pino I2C SDA
#define SCL_GPIO 22          // Pino I2C SCL
#define RESET_GPIO -1        // Pino de reset (não utilizado)

// Variáveis globais
SemaphoreHandle_t wificonnectedSemaphore;
SemaphoreHandle_t mqttconnectedSemaphore;
SemaphoreHandle_t displayMutex;

SSD1306_t dev;
static int temperature = 0;
static int humidity = 0;
static bool heating = false;
static bool scheduled = false;
static char status_msg[16] = "Iniciando"; // Buffer para mensagens de status

// Inicialização do display OLED
void display_init(void) {
    i2c_master_init(&dev, SDA_GPIO, SCL_GPIO, RESET_GPIO);
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xFF);
}

// Atualização segura do display
void update_display(void) {
    char line1[21] = {0}; // Linha 1: título
    char line2[21] = {0}; // Linha 2: temperatura e umidade
    char line3[21] = {0}; // Linha 3: estado do aquecimento
    char line4[21] = {0}; // Linha 4: status

    // Garante que as strings cabem nos buffers
    strncpy(line1, "Cafeteira Intel", sizeof(line1)-1);
    
    // Formatação segura com verificação de tamanho
    int needed = snprintf(line2, sizeof(line2), "Temp:%dC Umi:%d%%", temperature, humidity);
    if (needed >= sizeof(line2)) {
        ESP_LOGW("DISPLAY", "Linha 2 truncada");
    }
    
    needed = snprintf(line3, sizeof(line3), "Aquec:%s", heating ? "ON" : "OFF");
    if (needed >= sizeof(line3)) {
        ESP_LOGW("DISPLAY", "Linha 3 truncada");
    }
    
    // Status com formatação segura
    const char *prefix = "St:";
    size_t max_status = sizeof(line4) - strlen(prefix) - 1;
    strncpy(line4, prefix, sizeof(line4));
    strncat(line4, status_msg, max_status);

    ssd1306_clear_screen(&dev, false);
    ssd1306_display_text(&dev, 0, line1, strlen(line1), false);
    ssd1306_display_text(&dev, 2, line2, strlen(line2), false);
    ssd1306_display_text(&dev, 4, line3, strlen(line3), false);
    ssd1306_display_text(&dev, 6, line4, strlen(line4), false);
}

// Callback para comandos MQTT
void mqtt_event_handler_cb(const char *topic, const char *payload) {
    ESP_LOGI("MQTT_CMD", "Topic: %s, Payload: %s", topic, payload);
    
    if (strcmp(topic, "cafeteira/aquecimento") == 0) {
        heating = (strcmp(payload, "ligar") == 0);
        gpio_set_level(RELAY_PIN, heating ? 1 : 0);
        snprintf(status_msg, sizeof(status_msg), "Aq:%s", heating ? "ON" : "OFF");
    }
    else if (strcmp(topic, "cafeteira/agendamento") == 0) {
        scheduled = (strcmp(payload, "ativo") == 0);
        snprintf(status_msg, sizeof(status_msg), "Age:%s", scheduled ? "ON" : "OFF");
    }
    
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(1000))) {
        update_display();
        xSemaphoreGive(displayMutex);
    }
}

// Tarefa: Conexão WiFi -> MQTT
void wifiConnected(void *params) {
    while (1) {
        if (xSemaphoreTake(wificonnectedSemaphore, portMAX_DELAY)) {
            mqtt_start();
        }
    }
}

// Tarefa: Controle principal da cafeteira
void coffee_control_task(void *params) {
    char msg[50];
    bool last_button_state = false;
    bool current_button_state;

    xSemaphoreTake(mqttconnectedSemaphore, portMAX_DELAY);
    mqtt_sbscribe("cafeteira/aquecimento");
    mqtt_sbscribe("cafeteira/agendamento");

    while (1) {
        // Leitura do sensor DHT11
        struct dht11_reading data = DHT11_read();
        if (data.status == DHT11_OK) {
            temperature = data.temperature;
            humidity = data.humidity;
            
            // Publica dados via MQTT
            snprintf(msg, sizeof(msg), "{\"temp\":%d,\"umi\":%d}", temperature, humidity);
            mqtt_publish("cafeteira/sensor", msg);
        }

        // Controle do botão manual
        current_button_state = gpio_get_level(BUTTON_PIN);
        if (!current_button_state && last_button_state) {
            heating = !heating;
            gpio_set_level(RELAY_PIN, heating ? 1 : 0);
            snprintf(status_msg, sizeof(status_msg), "Manual:%s", heating ? "ON" : "OFF");
            mqtt_publish("cafeteira/aquecimento", heating ? "ligar" : "desligar");
        }
        last_button_state = current_button_state;

        // Atualiza display
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(1000))) {
            update_display();
            xSemaphoreGive(displayMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Atualiza a cada 1 segundo
    }
}

void app_main(void) {
    // Inicialização do NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Configuração dos GPIOs
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
    gpio_set_level(RELAY_PIN, 0); // Inicia com relé desligado

    // Inicialização dos periféricos
    DHT11_init(DHT_PIN);
    display_init();
    displayMutex = xSemaphoreCreateMutex();
    
    // Mensagem inicial no display
    ssd1306_display_text(&dev, 0, "Cafeteira Intel", 15, false);
    ssd1306_display_text(&dev, 2, "Inicializando...", 16, false);
    
    // Criação dos semáforos
    wificonnectedSemaphore = xSemaphoreCreateBinary();
    mqttconnectedSemaphore = xSemaphoreCreateBinary();

    // Inicia conexão WiFi
    wifi_start();

    // Criação das tarefas
    xTaskCreate(wifiConnected, "wifi_mqtt", 4096, NULL, 2, NULL);
    xTaskCreate(coffee_control_task, "control", 4096, NULL, 3, NULL);
}