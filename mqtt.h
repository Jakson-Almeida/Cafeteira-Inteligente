#ifndef MQTT_H
#define MQTT_H

void mqtt_start();
void mqtt_publish(char *topic, char *msg);
void mqtt_sbscribe(char *topic);

typedef void (*mqtt_data_callback_t)(const char* topic, const char* data);
void mqtt_set_callback(mqtt_data_callback_t cb);

#endif