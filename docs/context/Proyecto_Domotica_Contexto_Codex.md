# Proyecto de domótica y seguridad con ESP32 — contexto para Codex

**Responsable:** Morgan.  
**Actualización:** 19 de octubre de 2026.  
**Objetivo de este archivo:** transferir el contexto del proyecto para desarrollar en Codex el firmware del hub y de los tres nodos, como documento autónomo con los requisitos y conexiones vigentes.

## 1. De qué trata el proyecto

Es un sistema modular de domótica enfocado inicialmente en seguridad residencial, desarrollado para la materia de Automatización y Control y pensado también para usarse en casa. Es una capa adicional e independiente del ecosistema Tuya/Smart Home existente: Tuya seguirá siendo la plataforma principal; esta primera versión no necesita integrar ni modificar dispositivos Tuya.

El sistema tiene cuatro módulos físicos con ESP32:

1. **Hub con pantalla táctil:** interfaz gráfica, supervisión, armado/desarmado, recepción de eventos y notificaciones.
2. **Sensor de puerta:** contacto magnético MC-38 para informar apertura/cierre.
3. **Sensor de movimiento:** PIR para informar detección de movimiento.
4. **Lector RFID:** RC522 para solicitar armado/desarmado y mostrar el estado mediante LEDs.

Los nodos se comunicarán con el hub por **ESP-NOW**. El hub se conectará además al Wi-Fi doméstico para enviar notificaciones de WhatsApp mediante **CallMeBot**. La operación local debe continuar aunque no haya internet.

## 2. Estado actual real

**La fabricación y el ensamble de los cuatro módulos están terminados. Lo siguiente es desarrollar e integrar el software definitivo.**

Confirmado directamente por Morgan el 19 de octubre:

- Las cuatro PCB personalizadas están fabricadas.
- Los cuatro cases correspondientes están fabricados.
- Los cuatro módulos están armados.
- Todos fueron probados individualmente con códigos sencillos para verificar sus componentes.
- **Todavía no existe comunicación entre los módulos.** Los sketches actuales son pruebas locales, no firmwares integrados.

| Parte | Comprobado | Trabajo pendiente |
| --- | --- | --- |
| Hub | Pantalla y touch funcionan en pruebas | Interfaz definitiva, lógica global, ESP-NOW y WhatsApp |
| Puerta | Prueba local de apertura y LED | Firmware de nodo, filtrado y comunicación bidireccional |
| PIR | Prueba local de movimiento y LED | Firmware de nodo, eventos y sincronización |
| RFID | Lectura de tarjeta y alternancia local de LEDs | Solicitudes al hub, autorización y estado confirmado |
| Conjunto | Cuatro equipos ensamblados | Protocolo común y pruebas de integración |

No interpretar «todos probados» como validación de ESP-NOW, interfaz gráfica definitiva, alarmas coordinadas o entrega de mensajes por WhatsApp.

## 3. Alcance del siguiente desarrollo

El encargo comprende:

- Crear los gráficos y la aplicación del hub.
- Preparar e implementar en el hub la comunicación con los otros módulos mediante ESP-NOW.
- Agregar la funcionalidad de WhatsApp con la API de CallMeBot.
- Crear los firmwares de puerta, PIR y RFID compatibles con el hub.
- Documentar configuración, compilación, carga y pruebas.

Prioridad de trabajo: **hub y gráficos → contrato ESP-NOW → nodos e integración → WhatsApp y validación del conjunto**. Diseñar desde el principio la convivencia Wi-Fi/ESP-NOW, aunque el envío de WhatsApp se implemente después.

No agregar sirena, buzzer, app móvil, servidor externo propio o integración con Tuya como requisitos supuestos.

## 4. Hardware y restricciones generales

- Base: placas de desarrollo ESP32 clásico con ESP-WROOM-32. No asumir ESP32-S3/C3 ni PSRAM.
- Alimentación cableada, sin baterías. Mantener las conexiones físicas existentes.
- PCB diseñadas por Morgan; no reasignar GPIO para simplificar ejemplos de software.
- Usar los números GPIO en el código, no la posición física de los pines del DevKit.
- Conservar los sketches funcionales de diagnóstico antes de modificarlos.
- El firmware puede organizarse con Arduino C++; identificar la versión de Arduino-ESP32 realmente instalada y fijarla en la documentación.
- No imponer un cambio de entorno ni una biblioteca gráfica pesada sin una razón concreta.

## 5. Hub: pantalla y touch

### Hardware definitivo

El hub utiliza una **TFT IPS de 3.2 pulgadas, 240 × 320, SPI, ILI9341**, con touch capacitivo I2C manejado mediante los archivos del fabricante `FT6336.h` y `FT6336.cpp`.

Serigrafía: `3.2" TFT SPI 240X320 V1.0 Capacitive Touch`.

### Pinout definitivo del hub

| Pin pantalla | Señal | Conexión |
| --- | --- | --- |
| 1 | VCC | 5 V |
| 2 | GND | GND común |
| 3 | LCD_CS | GPIO18 |
| 4 | LCD_RST | GPIO19 |
| 5 | LCD_RS / DC | GPIO21 |
| 6 | SDI / MOSI | GPIO22 |
| 7 | SCK | GPIO23 |
| 8 | LED / backlight | GPIO32 |
| 9 | SDO / MISO | GPIO33 |
| 10 | CTP_SCL | GPIO25 |
| 11 | CTP_RST | GPIO26 |
| 12 | CTP_SDA | GPIO27 |
| 13 | CTP_INT | GPIO14 |
| 14 | SD_CS | GPIO13 |

Mantener `SD_CS` alto mientras no se use microSD. La microSD queda fuera de la primera implementación.

### Base funcional que debe preservarse

- LCD probado con SPI directo: `SPIClass busLCD(HSPI)`.
- Inicio: `busLCD.begin(23, 33, 22, -1)`; CS se controla por separado en GPIO18.
- Parámetros probados: `SPISettings(1000000, MSBFIRST, SPI_MODE0)`.
- Colores RGB565, byte alto primero; comandos de ventana `0x2A`, `0x2B` y escritura `0x2C`.
- Touch probado por sondeo, dirección I2C `0x38`, con lectura de coordenadas y dedos por Serial a 115200.
- Base táctil: `FT6336 touch(27, 25, 14, 26, 240, 320)`; `touch.begin()` y después `Wire.setClock(100000)`.
- Orientación inicial: `ROTATION_NORMAL`. Comprobar esquinas, centro, límites y orientación respecto al case antes de fijar botones.
- `begin()` del driver FT6336 devuelve `void`; no usarlo como booleano de éxito.
- El `touch.h` original del fabricante contiene otros pines; no copiarlo tal cual.
- `TFT_eSPI` no está validada como base de esta prueba. Tampoco asumir que se están utilizando `LCDWIKI_SPI` o `LCDWIKI_TOUCH`.

Conservar la secuencia exacta de inicialización LCD del sketch funcional. Se incluye más abajo. Este documento no sustituye al sketch ni incluye los archivos del driver.

Los archivos FT6336 requieren revisar errores I2C y límites de rotación antes de la UI definitiva. Un fallo de lectura no debe reutilizar coordenadas viejas ni activar botones.

Existe una limitación eléctrica identificada en los niveles de las líneas táctiles. El desarrollo se realizará con el hardware actual, por decisión de Morgan; no condicionar el firmware a rehacer la PCB o modificar SMD. El software no corrige los niveles eléctricos.

### Montaje y constantes del hub

El ESP32 está soldado directamente a la PCB y la pantalla está montada en socket. No asumir que las conexiones pueden intercambiarse como cables Dupont. Contar los pines desde VCC = 1 hasta SD_CS = 14, independientemente de cómo se gire la pantalla.

Constantes de referencia para centralizar en `board_pins.h`:

```cpp
constexpr uint8_t LCD_CS   = 18;
constexpr uint8_t LCD_RST  = 19;
constexpr uint8_t LCD_DC   = 21;
constexpr uint8_t LCD_MOSI = 22;
constexpr uint8_t LCD_SCK  = 23;
constexpr uint8_t LCD_MISO = 33;
constexpr uint8_t LCD_BL   = 32;
constexpr uint8_t CTP_SCL  = 25;
constexpr uint8_t CTP_RST  = 26;
constexpr uint8_t CTP_SDA  = 27;
constexpr uint8_t CTP_INT  = 14;
constexpr uint8_t SD_CS    = 13;
constexpr uint16_t ANCHO  = 240;
constexpr uint16_t ALTO   = 320;
```

### Secuencia LCD de referencia

Se incorpora la secuencia documentada para que el contexto general conserve los detalles de arranque. Mantener también el sketch original como referencia ejecutable.

Reset LCD: alto 50 ms, bajo 100 ms, alto 50 ms. Después, enviar esta secuencia en orden. Valores hexadecimales; “—” indica comando sin datos.

| Comando | Datos |
| --- | --- |
| CF | 00 C1 30 |
| ED | 64 03 12 81 |
| E8 | 85 00 78 |
| CB | 39 2C 00 34 02 |
| F7 | 20 |
| EA | 00 00 |
| C0 | 13 |
| C1 | 13 |
| C5 | 1C 35 |
| C7 | C8 |
| 21 | — |
| 36 | 08 |
| B6 | 0A A2 |
| 3A | 55 |
| F6 | 01 30 |
| B1 | 00 1B |
| F2 | 00 |
| 26 | 01 |
| E0 | 0F 35 31 0B 0E 06 49 A7 33 07 0F 03 0C 0A 00 |
| E1 | 00 0A 0F 04 11 08 36 58 4D 07 10 0C 32 34 0F |
| 11 | —; esperar 120 ms |
| 29 | —; esperar 50 ms |

CS baja durante comando/datos; DC baja para comando y alta para datos. Finalizar la transacción con CS alta. Mantener SD_CS alta mientras no se use la microSD. Configurar CS, DC, RST y BL como salidas, con estados iniciales definidos antes de iniciar el display.

Preservar esta inicialización junto con los parámetros de la prueba funcional.


### Integración y revisión del driver táctil

Configuración documentada de la prueba (fragmento, no sketch completo):

```cpp
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "FT6336.h"

FT6336 touch(CTP_SDA, CTP_SCL, CTP_INT, CTP_RST, ANCHO, ALTO);
// En setup:
touch.begin();             // Dirección predeterminada: 0x38
Wire.setClock(100000);     // Después de begin(), que inicializa Wire
touch.setRotation(ROTATION_NORMAL);
// En la tarea periódica de lectura:
touch.read();
if (touch.isTouched && touch.touches > 0) {
    const uint16_t x = touch.points[0].x;
    const uint16_t y = touch.points[0].y;
    // Validar límites y transformar a coordenadas de la interfaz.
}
```


- Constructor: `FT6336(uint8_t sda, uint8_t scl, uint8_t intPin, uint8_t rst, uint16_t width, uint16_t height)`.
- Propiedades públicas documentadas: `isTouched`, `touches` y `points[2]`; cada punto contiene `x`, `y`, `id` y `size`.
- Si los archivos están junto al sketch, usar `#include "FT6336.h"` tanto en el sketch como dentro de `FT6336.cpp`.
- El `touch.h` del ejemplo define SCL25, SDA32, INT−1 y RST33. El PCB requiere SCL25, SDA27, INT14 y RST26. Instanciar el driver directamente evita heredar esa configuración incorrecta.
- No pasar −1 al constructor del touch: los pines son `uint8_t`, el valor se convierte en 255 y ese driver no maneja pines ausentes. Esto no cambia el uso válido de −1 en `SPIClass.begin()` para CS gestionado manualmente.
- Revisar `Wire.endTransmission()`, cantidad recibida por `requestFrom()` y disponibilidad de bytes antes de aceptar una lectura. `begin()` no devuelve estado; comprobar presencia y errores I2C explícitamente.
- Las fórmulas de rotación documentadas usan `width - x` y `height - y`; revisar índices de base cero y validar `0 <= x < ancho` y `0 <= y < alto` según la orientación elegida.
- CTP_INT sigue conectado a GPIO14 aunque se utilice sondeo sin ISR.
- Propuesta para botones: activar al soltar dentro del mismo botón, cancelar al salir de él e ignorar acciones con más de un dedo en la primera versión.

### Rendimiento y alcance de las pruebas gráficas

La prueba alternaba rojo, verde, azul, blanco y negro cada 1500 ms. Esas esperas son de diagnóstico y no deben incorporarse al bucle normal de la aplicación.

A 1 MHz, transferir 240 × 320 píxeles RGB565 necesita aproximadamente 1.23 s solo de datos. Una fila ocupa 480 bytes y una imagen completa 153600 bytes. Usar filas o bloques pequeños, actualización parcial y memoria acotada. No subir directamente a 40/80 MHz: ensayar incrementos después de preservar la base y registrar la frecuencia validada en este PCB.

Objetivo inicial propuesto: sondeo táctil cada 20–30 ms, con `millis()` y diferencias sin signo. Evitar que una transferencia larga del display impida atender touch y radio. No hacer I2C, SPI, dibujado ni HTTP dentro de callbacks ESP-NOW.

Se puede evaluar Adafruit_GFX como capa de texto y primitivas sobre el driver propio, pero no es una dependencia ya probada. Tampoco migrar directamente a LVGL o TFT_eSPI sin conservar la referencia. `SPI.h` y `Wire.h` vienen con el core; `spi_dev.h` del demo no es necesario para la base con `SPIClass`.

No hay RTC ni sincronización horaria configurados. Hasta implementar y validar hora real, registrar eventos con sesión y tiempo desde el arranque, sin inventar fechas.

### Interfaz prevista

**Toda la interfaz del hub debe estar en inglés**, incluidos menús, botones, estados, avisos y diagnósticos visibles. El documento de contexto puede permanecer en español. Diseño legible y operable con el dedo:

- **Home:** estado global, armado/desarmado, puerta, movimiento y conectividad de nodos.
- **Nodes:** identificación, último estado y tiempo desde la última comunicación.
- **Events:** historial acotado de aperturas, movimiento, RFID, cambios de estado y fallos de comunicación.
- **Diagnostics / Settings:** MAC, canal, Wi-Fi, estado del servicio de notificaciones y versión del firmware, sin mostrar secretos.

Etiquetas de referencia: `Arm`, `Disarm`, `Armed`, `Disarmed`, `Exit delay`, `Alarm`, `Door open`, `Door closed`, `Motion detected`, `No motion`, `Online`, `Offline` y `Unknown`.

Una pulsación sostenida debe producir una sola acción. Evitar repintar la pantalla completa continuamente; actualizar regiones modificadas. No reservar un framebuffer completo por defecto ni asumir memoria adicional disponible.

## 6. Nodo de puerta

Sensor magnético **MC-38**, con contacto entre la entrada y GND y `INPUT_PULLUP`.

| Señal | GPIO | Dirección |
| --- | --- | --- |
| Sensor magnético | 27 | Entrada |
| LED de estado | 16 | Salida |

El firmware deberá filtrar rebotes, publicar estado inicial y cambios, enviar heartbeat y recuperar sincronización tras reinicios. Verificar abierta/cerrada con el sensor real.

**Función del LED:** encendido únicamente cuando el sistema esté activo (armado) y el nodo tenga comunicación vigente con el hub. La apertura del sensor no gobierna el LED.

## 7. Nodo PIR

Sensor con marcas **HC-SR501 / RCWL-0507**, conexiones VCC, GND y OUT; LED con resistencia de 220 Ω.

| Señal | GPIO | Dirección |
| --- | --- | --- |
| OUT del PIR | 27 | Entrada |
| LED de estado | 25 | Salida |

El firmware deberá contemplar estabilización inicial del sensor, lectura periódica, cambios de estado, heartbeat y sincronización con el estado global. No emitir una alarma nueva en cada iteración mientras OUT permanezca activo. Mantener configurables los tiempos y documentar el modo físico del sensor utilizado.

**Función del LED:** encendido únicamente cuando el sistema esté activo (armado) y el nodo tenga comunicación vigente con el hub. La detección de movimiento no gobierna el LED.

### Regla compartida de los LEDs de puerta y PIR

```cpp
ledOn = systemArmed && hubConnected && globalStateKnown;
```

- Encendido fijo: sistema armado, estado global conocido y enlace vigente.
- Apagado: desarmado, sin conexión, estado desconocido o arranque sin sincronizar.
- `hubConnected` se determina por mensajes válidos recientes recibidos del hub y un timeout; no basta con registrar el peer ni con un envío exitoso.
- El hub debe enviar sincronización o heartbeat periódicos hacia los nodos para que puedan verificar su presencia.
- Al vencer el timeout, apagar el LED y esperar una nueva confirmación antes de encenderlo.
- Para la máquina de estados propuesta, considerar activo tanto `ARMED` como `ALARM`; `EXIT_DELAY` aún no representa armado completo. La pérdida de internet por sí sola no apaga estos LEDs si el enlace local con el hub sigue vigente.

## 8. Nodo RFID

Lector **RC522 / MFRC522**, alimentación a 3.3 V, SPI con pines personalizados. Pinout del módulo:

| Señal | GPIO |
| --- | --- |
| SS / SDA del RC522 (selección SPI) | 21 |
| RST | 22 |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| IRQ | 27 |
| LED verde | 25 |
| LED rojo | 26 |

La etiqueta SDA del lector corresponde a SS en su uso SPI, no al SDA del bus I2C. Inicializar explícitamente con `SPI.begin(18, 19, 23, 21)` (SCK, MISO, MOSI, SS) y configurar RST en GPIO22. Estos GPIO pertenecen al ESP32 del nodo RFID; cada módulo tiene su propio pinout independiente.

Prueba local: al pasar la tarjeta se alterna entre desactivado (verde encendido) y activado (rojo encendido). Esa alternancia **todavía no controla al hub**.

Firmware definitivo:

- Inicializar SPI con los pines reales; conservar IRQ aunque la biblioteca opere por sondeo.
- Evitar múltiples solicitudes por mantener una tarjeta frente al lector.
- Enviar una solicitud identificable al hub; el hub decide y confirma el estado resultante.
- Los LEDs deben representar el estado global confirmado, no un cambio local optimista.
- Definir una indicación diferenciada de desconexión/pendiente para no mostrar un estado viejo como vigente.
- Implementar lista de tarjetas autorizadas; no asumir que cualquier tarjeta debe poder desarmar.
- Decidir dónde se valida la lista; propuesta inicial: decisión central en el hub.

## 9. Lógica del sistema: propuesta por implementar

El **hub será la autoridad del estado global**. Los sensores reportan hechos y el RFID solicita acciones. Ningún nodo debe mantener una decisión independiente de armado que contradiga al hub.

| Estado propuesto | Comportamiento |
| --- | --- |
| DISARMED | Mostrar y registrar sensores sin generar alarma de intrusión |
| EXIT_DELAY, opcional | Cuenta regresiva configurable antes del armado |
| ARMED | Evaluar apertura y movimiento como disparadores |
| ALARM | Mostrar origen, registrar y solicitar notificación; permanecer hasta desarmado autorizado |

Estas reglas son una base de implementación, no funciones ya comprobadas. Valores de tiempos, política tras reinicio, armado con puerta abierta, nodos obligatorios y autorización del touch deben quedar configurables y confirmarse para la versión final.

Como punto de partida: inicio desarmado tras reinicio, sensores desconocidos hasta recibir datos, advertencia si un nodo desaparece y bloqueo del armado cuando falte información de un sensor obligatorio. No confundir «sin comunicación» con «puerta cerrada» o «sin movimiento».

## 10. ESP-NOW y convivencia con Wi-Fi

Topología en estrella: puerta, PIR y RFID ↔ hub. El acceso a internet se concentra en el hub.

Crear un protocolo común versionado y reutilizado por los cuatro firmwares. Propuesta:

| Mensaje | Finalidad |
| --- | --- |
| HELLO | Registro/reaparición de un nodo y su sesión de arranque |
| STATUS / HEARTBEAT | Estado actual y disponibilidad |
| EVENT | Cambio de sensor o presentación de tarjeta |
| COMMAND / REQUEST | Solicitud explícita de armado/desarmado |
| ACK | Confirmación de aplicación vinculada a una solicitud |
| STATE_SYNC | Estado global y revisión vigente |

Incluir identificador de protocolo, versión, tipo de mensaje, ID/tipo de nodo, sesión de arranque, secuencia y longitud. Validar longitud, remitente y versión antes de actuar. Usar un formato serializado definido y mensajes pequeños; no enviar `String`, punteros ni estructuras con padding no controlado.

Requisitos de implementación:

- Deduplicación: una retransmisión RFID no puede alternar otra vez el sistema.
- Preferir `ARM` y `DISARM`; si se usa una solicitud de alternancia, procesarla una sola vez y devolver el resultado anterior ante duplicados.
- ACK de aplicación para acciones críticas, con reintentos limitados y espera configurable.
- Heartbeat y timeout de desconexión; punto de partida propuesto: 5 s y 15 s, respectivamente.
- Resincronización de estado y LEDs al reiniciar o reconectar cualquier nodo.
- Colas acotadas; callbacks breves que copien/enfilen datos. Procesar lógica y gráficos fuera del callback.
- Prever unicast cifrado para órdenes sensibles y claves fuera del repositorio. Una MAC permitida no equivale por sí sola a autenticación criptográfica.

**Canal de radio:** el hub y los nodos deben compartir canal. Cuando el hub está conectado al AP doméstico, diseñar ESP-NOW alrededor del canal de ese AP. No fijar un canal independiente incompatible. Documentar canal inicial, arranque sin AP y recuperación si cambia el canal; no asumir que la reconexión Wi-Fi resuelve automáticamente a los nodos.

Las MAC reales, canal y claves no están proporcionados. Usar configuración de ejemplo y un diagnóstico para obtener MAC, sin inventar valores reales.

## 11. Notificaciones WhatsApp con CallMeBot

**Proveedor elegido por Morgan:** https://www.callmebot.com/. La integración forma parte del firmware del hub.

Documentación consultada: [API de WhatsApp de CallMeBot](https://www.callmebot.com/es/blog/api-gratis-mensajes-whatsapp/).

El servicio documenta este endpoint para mensajes de texto al número activado:

```text
https://api.callmebot.com/whatsapp.php?phone=<NUMERO>&text=<MENSAJE_URLENCODED>&apikey=<API_KEY>
```

Requiere número con código de país y API key obtenida mediante activación. Codificar los parámetros de la URL, incluidos mensajes con acentos y saltos de línea. La documentación lo presenta para uso personal; no asumir recepción de comandos por WhatsApp.

Al consultar la página el 19 de octubre de 2026, la activación mostraba disponibilidad limitada del bot. No consta si Morgan ya cuenta con una clave; verificarlo cuando se configure, sin bloquear el desarrollo local.

Diseño requerido para el firmware:

- Credenciales Wi-Fi, teléfono y API key fuera del código versionado; proporcionar archivo de ejemplo sin secretos.
- Usar HTTPS con validación de certificado; no dejar validación desactivada como solución definitiva.
- No registrar URLs completas que contengan API key.
- Aislar las peticiones HTTP/TLS de la UI y del procesamiento ESP-NOW, con timeouts y cola acotada.
- Generar una notificación por incidente, no por lectura repetida; aplicar deduplicación y límite de frecuencia configurable.
- Reintentos limitados; considerar que un timeout puede ocurrir después de que el servicio aceptó el mensaje.
- Registrar resultado HTTP y error sin afirmar entrega al teléfono solo por aceptar la solicitud.
- Si no hay internet, la alarma local y la pantalla siguen funcionando. Marcar notificación pendiente/fallida y definir caducidad para no enviar alertas antiguas como actuales.
- Añadir prueba de envío explícita cuando estén configuradas las credenciales.

Eventos iniciales propuestos: alarma por apertura o movimiento estando armado. Avisos de armado/desarmado y desconexión pueden ser opciones; no enviar cada heartbeat ni cada detección estando desarmado.

## 12. Organización del repositorio

Usar **`hardware/`** para concentrar el diseño y la fabricación física, y **`firmware/`** para todo el software de los ESP32. `hardware/` incluye electrónica, archivos de fabricación y diseño mecánico de los cases.

Conservar una carpeta KiCad conjunta para los diseños de todos los módulos y una carpeta por módulo para sus archivos de fabricación y case. Estructura recomendada:

| Ruta | Contenido |
| --- | --- |
| `firmware/common/` | Protocolo, tipos y componentes compartidos |
| `firmware/hub/` | Display, touch, UI, estado, radio y notificaciones |
| `firmware/door/` | MC-38, LED de estado y comunicación |
| `firmware/pir/` | PIR, LED de estado y comunicación |
| `firmware/rfid/` | RC522, LEDs y solicitudes |
| `hardware/kicad/` | Carpeta existente con los diseños PCB de los cuatro módulos |
| `hardware/hub/laser/` | Archivos de láser para fabricar la PCB del hub |
| `hardware/hub/cnc/` | DXF para Fusion/CNC y archivos CAM asociados |
| `hardware/hub/enclosure/` | STEP del case y fuentes CAD/exportaciones disponibles |
| `hardware/door/laser/`, `cnc/`, `enclosure/` | Misma organización dentro de `hardware/door/` |
| `hardware/pir/laser/`, `cnc/`, `enclosure/` | Misma organización dentro de `hardware/pir/` |
| `hardware/rfid/laser/`, `cnc/`, `enclosure/` | Misma organización dentro de `hardware/rfid/` |
| `docs/` | Pines, BOM, ensamble, configuración y pruebas |
| `README.md` | Presentación del proyecto y guía de entrada |
| `.gitignore` | Exclusión de secretos, compilaciones, cachés y temporales |

Esta es una propuesta de organización para el repositorio; no implica que las carpetas se hayan movido. Al organizar los archivos reales, conservar las referencias de KiCad y los archivos fuente editables junto con las exportaciones necesarias para fabricar. Mantener nombres en inglés y consistentes con los módulos del firmware.

Separar dentro del hub: `board_pins`, driver LCD, entrada touch, vistas, máquina de estados, transporte ESP-NOW, registro de nodos, historial y notificaciones. Mantener la UI independiente del protocolo y de HTTP.

Bibliotecas de base o candidatas:

| Elemento | Uso / estado |
| --- | --- |
| Arduino.h, SPI.h, Wire.h | Base ya utilizada |
| FT6336.h/.cpp | Driver local probado; conservar archivos |
| Biblioteca del sketch RC522 | Reutilizar la que ya funciona y registrar versión |
| WiFi.h, esp_now.h | Comunicación por implementar |
| Cliente TLS y HTTP del core elegido | CallMeBot por implementar |
| Preferences.h | Candidata para configuración persistente |
| Capa gráfica ligera | Por implementar sobre base LCD funcional; no hay biblioteca definitiva validada |

Evitar `delay()` largos durante operación normal. Acotar historial, colas y memoria; persistir configuración solo cuando cambie. Comprobar las firmas ESP-NOW contra la versión instalada del core. No declarar compatibilidad o compilación sin verificarla.

## 13. Etapas y criterios de aceptación

1. **Preservar referencia:** reunir los cuatro sketches funcionales, driver FT6336 y versión del entorno. Mantener una copia de diagnóstico por módulo.
2. **Hub local:** pantalla inicial, estados y navegación íntegramente en inglés; botones con una sola acción por pulsación y orientación correcta en el case. Simulaciones claramente identificadas.
3. **Contrato y radio:** definir protocolo, configurar peers y probar un nodo real. Validar tramas, ACK y duplicados.
4. **Puerta y PIR:** reflejar estados, reconexión, reinicio y pérdida de comunicación sin estados normales falsos. Comprobar LED encendido solo con armado y conexión vigentes; apagarlo al desarmar o perder al hub, y resincronizarlo al recuperar el enlace.
5. **RFID:** tarjeta autorizada solicita cambio; hub lo confirma; LEDs quedan sincronizados. Una petición duplicada no ejecuta otra acción.
6. **WhatsApp:** probar envío real, caracteres especiales, ausencia de internet, errores y recuperación sin congelar UI o sensores.
7. **Integración:** sensor activo desarmado, alarma armado, desarmado, reinicios del hub/nodos, pérdidas de mensajes y operación sin internet.
8. **Entrega:** instrucciones, dependencias fijadas, configuración de ejemplo, tabla de pruebas realizadas y limitaciones pendientes.

Las pruebas físicas las confirma Morgan. Compilar correctamente no demuestra funcionamiento en la PCB; registrar por separado revisión, compilación y prueba real.

## 14. Información que Codex debe obtener al empezar

- Sketch funcional del hub y archivos `FT6336.h` / `FT6336.cpp`.
- Sketches actuales de puerta, PIR y RFID.
- Entorno de compilación y versión de Arduino-ESP32.
- MAC de los cuatro dispositivos, red Wi-Fi y estrategia de canal.
- Tarjetas autorizadas y política de armado. Los pinouts y la función de los LEDs de puerta/PIR están definidos en este documento.
- Teléfono y API key de CallMeBot mediante configuración local, sin publicarlos.

Solicitar únicamente lo que impida el siguiente paso concreto. La ausencia de credenciales o MAC no impide crear la UI, el protocolo y los ejemplos de configuración. La falta de un sketch funcional sí debe resolverse antes de sustituir a ciegas el driver que ya funciona.

## 15. Referencias técnicas

Este documento reúne el estado actual, los pinouts confirmados por Morgan y los requisitos de implementación. Codex no necesita consultar conversaciones anteriores. Los sketches funcionales y archivos del driver deben acompañar al proyecto como base ejecutable.

Los GPIO indicados aquí son los de las PCB del proyecto. Si un archivo de ejemplo usa otros pines, adaptarlo a estas tablas. Si un archivo del proyecto contradice una tabla, señalar el conflicto concreto antes de cambiar conexiones o dar por válido otro pinout.

- [CallMeBot: API de WhatsApp](https://www.callmebot.com/es/blog/api-gratis-mensajes-whatsapp/).
- [Espressif: ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html). Consultar la versión correspondiente al entorno instalado.
- [LCDWiki: pantalla ILI9341 de 3.2 pulgadas](https://www.lcdwiki.com/3.2inch_IPS_SPI_Module_ILI9341).
- [Instrucciones del demo ESP32 MSP3222/MSP3223](https://www.lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_MSP3222_MSP3223_ESP32_Demo_Instructions_EN.pdf).
- [Esquema MSP3222/MSP3223](https://www.lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_Module_MSP3222_MSP3223_Schematic.pdf).
- [Manual MSP3222/MSP3223](https://www.lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_Module_MSP3222_MSP3223_User_Manual_EN.pdf).

La familia MSP3222/MSP3223 es una referencia documental, no una identificación comercial física demostrada. Del mismo modo, FT6336 identifica el driver operativo; no asegurar un sufijo concreto del chip sin comprobar su identificación.

## 16. Instrucción de inicio para Codex

> Lee este documento y los archivos existentes. Estoy desarrollando cuatro módulos ESP32 para seguridad doméstica: hub táctil, puerta, PIR y RFID. Las cuatro PCB y sus cases ya están fabricados, armados y probados individualmente, pero aún no se comunican. Quiero desarrollar el software definitivo: gráficos e interfaz del hub completamente en inglés, lógica global, ESP-NOW, notificaciones WhatsApp con CallMeBot y firmware de los tres nodos. Usa los pinouts de este documento y conserva la base funcional de los drivers. Los LEDs de puerta y PIR deben encenderse solo con sistema armado y conexión vigente al hub. Separa el repositorio en firmware/ para software y hardware/ para KiCad, fabricación y cases. Empieza revisando el entorno y la base de pantalla/touch; después implementa por etapas. Comparte el protocolo entre todos los firmwares, haz que el hub controle el estado global y evita acciones duplicadas. Diseña Wi-Fi y ESP-NOW para convivir y no bloquees la operación local cuando falle internet. Separa hechos confirmados de propuestas, no inventes pines ni credenciales y distingue compilación de pruebas físicas. Avanza con lo disponible y pide solo los archivos o datos necesarios para el siguiente paso.
