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
#include "mqtt_handler.h"

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
static SemaphoreHandle_t buttonSemaphore;
static volatile bool buttonPressed = false; // 'volatile' para acesso seguro entre ISR e task

SSD1306_t dev;
static int temperature = 0;
static int humidity = 0;
static bool heating = false;
static bool scheduled = false;
static char status_msg[16] = "Iniciando"; // Buffer para mensagens de status

static int last_temp = -1, last_humidity = -1; // Variáveis estáticas para armazenar os últimos valores

void mqtt_data_handler(const char* topic, const char* data) {
    mqtt_event_handler_cb(topic, data); // Usa a função existente
}

// Interrupcao
void IRAM_ATTR button_isr_handler(void* arg) {
    static uint32_t lastInterruptTime = 0;
    uint32_t now = xTaskGetTickCountFromISR();
    
    // Debounce de 50ms (ajuste conforme necessário)
    if ((now - lastInterruptTime) > pdMS_TO_TICKS(50)) {
        buttonPressed = true;
        xSemaphoreGiveFromISR(buttonSemaphore, NULL); // Notifica a task
    }
    lastInterruptTime = now;
}

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
            mqtt_set_callback(mqtt_data_handler);
        }
    }
}

// Tarefa Botao
void button_task(void* arg) {
    while(1) {
        if(xSemaphoreTake(buttonSemaphore, portMAX_DELAY) == pdTRUE) {
            if(buttonPressed) {
                buttonPressed = false;
                
                // Lógica segura do botão
                heating = !heating;
                gpio_set_level(RELAY_PIN, heating ? 1 : 0);
                
                // Atualiza display (com mutex)
                if(xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100))) {
                    snprintf(status_msg, sizeof(status_msg), "Aq:%s", heating ? "ON" : "OFF");
                    update_display();
                    xSemaphoreGive(displayMutex);
                }
                
                // Publica via MQTT (não bloqueante)
                mqtt_publish("cafeteira/aquecimento", heating ? "ligar" : "desligar");
            }
        }
    }
}

// Tarefa: Controle principal da cafeteira
void coffee_control_task(void *params) {
    char msg[50];

    xSemaphoreTake(mqttconnectedSemaphore, portMAX_DELAY);
    mqtt_sbscribe("cafeteira/aquecimento");
    mqtt_sbscribe("cafeteira/agendamento");

    while (1) {
        // Leitura do sensor DHT11
        struct dht11_reading data = DHT11_read();
        if (data.status == DHT11_OK) {
            // Publica apenas se os valores mudaram
            if (data.temperature != last_temp || data.humidity != last_humidity) {
                temperature = data.temperature;
                humidity = data.humidity;
                snprintf(msg, sizeof(msg), "{\"temp\":%d,\"umi\":%d}", temperature, humidity);
                mqtt_publish("cafeteira/sensor", msg);
                last_temp = temperature;
                last_humidity = humidity;
            }
        }

        // Atualiza display
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(1000))) {
            update_display();
            xSemaphoreGive(displayMutex);
            // ESP_LOGI("TASK_CONTROL", "Display atualizado");
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Atualiza a cada 2 segundo
    }
}

void app_main(void) {
    // Inicialização do NVS
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Configuração dos GPIOs
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 0); // Inicia com relé desligado
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Detecta borda de descida (botão pressionado)
    };
    gpio_config(&io_conf);

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
    buttonSemaphore = xSemaphoreCreateBinary();

    // Inicia conexão WiFi
    wifi_start();

    // Instala serviço de interrupção
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1); // Prioridade 1
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    // Criação das tarefas
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, NULL);       
    xTaskCreate(coffee_control_task, "control", 4096, NULL, 3, NULL);   
    xTaskCreate(wifiConnected, "wifi_mqtt", 4096, NULL, 2, NULL);
}