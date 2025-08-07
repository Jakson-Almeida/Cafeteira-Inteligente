# Documentação de Agendamento - Cafeteira Inteligente

## Visão Geral
Este documento descreve a implementação da lógica de agendamento na cafeteira inteligente usando o ESP32, sincronizando o tempo via NTP e permitindo o controle programado das operações.

## 1. Configuração Inicial

### 1.1 Pré-requisitos
- ESP-IDF v4.4 ou superior
- Conexão WiFi estável
- Biblioteca `lwIP` com suporte a SNTP habilitado

### 1.2 Componentes Necessários
```c
#include "esp_sntp.h"
#include "time.h"
```

## 2. Sincronização de Tempo

### 2.1 Configuração NTP
```c
#define NTP_SERVER "pool.ntp.org"
#define TIMEZONE "UTC-3"  // Fuso horário do Brasil
#define DAYLIGHT_SAVING 0 // Sem horário de verão

void initialize_sntp() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
    
    setenv("TZ", TIMEZONE, 1);
    tzset();
}
```

### 2.2 Verificação de Sincronização
```c
void wait_for_time_sync() {
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    if (retry == retry_count) {
        ESP_LOGE("SNTP", "Falha na sincronização");
    } else {
        ESP_LOGI("SNTP", "Tempo sincronizado");
    }
}
```

## 3. Lógica de Agendamento

### 3.1 Estrutura de Agendamento
```c
typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
    void (*action)(void);
} schedule_t;
```

### 3.2 Exemplo de Implementação
```c
// Array de agendamentos
schedule_t schedules[] = {
    {7, 0, true, start_brewing},    // Café às 7:00
    {7, 30, true, stop_brewing},    // Desliga às 7:30
    {12, 0, false, start_brewing}   // Café ao meio-dia (desativado)
};

void check_schedules() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    for (int i = 0; i < sizeof(schedules)/sizeof(schedule_t); i++) {
        if (schedules[i].enabled && 
            timeinfo.tm_hour == schedules[i].hour && 
            timeinfo.tm_min == schedules[i].minute &&
            timeinfo.tm_sec == 0) {
            schedules[i].action();
        }
    }
}
```

## 4. Integração com MQTT

### 4.1 Comandos de Agendamento
```json
{
    "hour": 7,
    "minute": 0,
    "action": "start",
    "enabled": true
}
```

### 4.2 Handler MQTT
```c
void mqtt_schedule_handler(const char* payload) {
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) return;

    int hour = cJSON_GetObjectItem(root, "hour")->valueint;
    int minute = cJSON_GetObjectItem(root, "minute")->valueint;
    bool enabled = cJSON_GetObjectItem(root, "enabled")->valueint;
    const char *action = cJSON_GetObjectItem(root, "action")->valuestring;

    // Adiciona/atualiza agendamento
    update_schedule(hour, minute, action, enabled);
    
    cJSON_Delete(root);
}
```

## 5. Implementação na Task Principal

### 5.1 Task de Controle
```c
void coffee_control_task(void *params) {
    // ... inicialização ...

    while (1) {
        // Verifica sensores
        read_sensors();
        
        // Verifica agendamentos
        check_schedules();
        
        // Atualiza display
        update_display();
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

## 6. Melhores Práticas

1. **Sincronização Periódica**: Configure o SNTP para sincronizar periodicamente (padrão: a cada hora)
   ```c
   sntp_set_sync_interval(3600000); // 1 hora em ms
   ```

2. **Persistência**: Armazene agendamentos na NVS para sobreviver a reinicializações

3. **Segurança**: Valide todos os dados recebidos via MQTT

4. **Fuso Horário**: Atualize automaticamente para horário de verão quando aplicável

5. **Logging**: Registre todas as ações de agendamento para diagnóstico

## 7. Exemplo Completo

```c
void app_main() {
    // Inicializa WiFi
    wifi_init();
    
    // Configura NTP
    initialize_sntp();
    wait_for_time_sync();
    
    // Inicia MQTT
    mqtt_init();
    mqtt_subscribe("cafeteira/agendamento", mqtt_schedule_handler);
    
    // Cria task de controle
    xTaskCreate(coffee_control_task, "control", 4096, NULL, 3, NULL);
}
```

## 8. API de Agendamento

### Endpoints MQTT:
- `cafeteira/agendamento/set` - Configura novo agendamento
- `cafeteira/agendamento/list` - Lista agendamentos ativos
- `cafeteira/agendamento/remove` - Remove agendamento

### Formato JSON:
```json
{
    "id": 1,
    "time": "07:00",
    "action": "start",
    "days": [1,2,3,4,5], // Dias da semana (1=Segunda)
    "enabled": true
}
```

Esta documentação fornece a base para implementar um sistema de agendamento robusto na cafeteira inteligente, permitindo controle preciso baseado em tempo real sincronizado com servidores NTP.
