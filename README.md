
# Cafeteira Inteligente com ESP32

![Diagrama do Projeto](https://example.com/cafeteira-diagram.png) **(Adicionar imagem real posteriormente)***

Projeto de uma cafeteira controlada via WiFi/MQTT com monitoramento de temperatura e umidade, acionamento remoto e display OLED integrado.

## Funcionalidades

- **Controle Remoto**:
  - Liga/desliga via MQTT ou botão físico
  - Agendamento de funcionamento
- **Monitoramento**:
  - Sensor DHT11 (temperatura e umidade)
  - Display OLED 128x64 com status em tempo real
- **Conectividade**:
  - WiFi para conexão à rede local
  - MQTT para comunicação com broker (local/remoto)

## Hardware

| Componente       | ESP32 Pin | Descrição               |
|------------------|-----------|-------------------------|
| DHT11            | GPIO4     | Sensor temperatura/umidade |
| OLED SSD1306     | GPIO21 (SDA), GPIO22 (SCL) | Display I2C |
| Relé             | GPIO2     | Controle da resistência |
| Botão            | GPIO3     | Acionamento manual      |

##Configuração MQTT

### Tópicos
| Tópico                     | Direção   | Formato                 | Descrição               |
|----------------------------|-----------|-------------------------|-------------------------|
| `cafeteira/aquecimento`    | Sub       | `"ligar"`/`"desligar"` | Controle do relé        |
| `cafeteira/agendamento`    | Sub       | `"ativo"`/`"inativo"`  | Habilita agendamento    |
| `cafeteira/sensor`         | Pub       | `{"temp":17,"umi":52}`  | Dados do sensor (JSON)  |

### Broker Recomendado
```ini
URL: mqtt://broker.hivemq.com:1883
QoS: 0 (at most once)
```

## Configuração

1. **Variáveis de Ambiente**:
   ```bash
   cp sdkconfig.defaults sdkconfig
   idf.py menuconfig
   ```
   - Defina `CONFIG_BROKER_URL` no menu `ESP-MQTT Configuration`

2. **Thresholds** (em `main.c`):
   ```c
   #define TEMP_THRESHOLD 1      // Variação mínima de temperatura para publicação (°C)
   #define HUMIDITY_THRESHOLD 1  // Variação mínima de umidade (%)
   #define PUBLISH_INTERVAL 30000 // Intervalo entre publicações (ms)
   ```

## Instalação

```bash
# Clone o repositório
git clone https://github.com/seu-usuario/cafeteira-inteligente.git
cd cafeteira-inteligente

# Configure o ambiente ESP-IDF
. $HOME/esp/esp-idf/export.sh

# Compile e grave
idf.py build flash monitor
```

## Debug

### Logs Importantes
```log
I (11728) MQTT: Subscrito no tópico cafeteira/aquecimento
I (20248) MQTT: Publicado {"temp":17,"umi":52}
E (49118) DHT11: Erro na leitura (Código: 5)
```

### Ferramentas Recomendadas
- **MQTT Explorer**: Monitoramento em tempo real
- **ESP-IDF Monitor**: Logs detalhados do ESP32
- **Wireshark**: Análise de tráfego (opcional)

## Boas Práticas Implementadas

1. **Otimização MQTT**:
   - Publicação somente com variação relevante
   - QoS 0 para reduzir tráfego
   - Formato JSON validado

2. **Estabilidade**:
   - Mutex para acesso ao display
   - Tratamento de erros do DHT11
   - Timeout após falhas

3. **Segurança**:
   - Sem credenciais hardcoded
   - WiFi com WPA2

## Exemplo de Saída

```json
{
  "temp": 17,
  "umi": 52,
  "status": "aquecimento_off",
  "ultima_atualizacao": "2025-07-31T11:17:08"
}
```

## Licença

MIT License - Consulte o arquivo [LICENSE](LICENSE) para detalhes.
```

---

### Melhorias Sugeridas (To-Do)
- [ ] Adicionar diagrama esquemático
- [ ] Implementar OTA updates
- [ ] Adicionar autenticação MQTT
- [ ] Suporte à nuvem (AWS IoT/Azure)

Este README fornece uma visão completa do projeto, incluindo:
- Configuração técnica detalhada
- Instruções de instalação
- Estratégias de debug
- Boas práticas implementadas
- Roadmap de melhorias

Você pode personalizar com:
1. Imagens reais do projeto
2. Vídeo de demonstração
3. Exemplos de comandos MQTT
4. Informações específicas do seu ambiente
