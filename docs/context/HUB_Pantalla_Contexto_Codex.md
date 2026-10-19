# HUB de pantalla ESP32 — contexto y plan para Codex

Fecha de corte: 18 de octubre de 2026. Usuario: Morgan. Alcance: exclusivamente el módulo HUB con pantalla; los otros nodos se describen únicamente como interfaces de comunicación.

## 1. Encargo para Codex

Continuar el desarrollo del firmware del HUB de un proyecto académico de domótica y seguridad. Conservar el hardware y la prueba de pantalla/touch que ya funcionan. Construir una interfaz táctil para consultar sensores, armar y desarmar el sistema, recibir eventos por ESP-NOW y sincronizar el estado con otros ESP32.

Este documento distingue **confirmado** (hardware, conexiones y pruebas) de **propuesto** (arquitectura y comportamiento por implementar). No presentar las propuestas como funcionalidades existentes.

El sistema es una capa adicional a Tuya/Smart Home; no reemplaza ni integra directamente esos equipos en esta primera versión. El HUB debe funcionar localmente sin internet. Las notificaciones por WhatsApp se contemplan para una etapa posterior; no se han proporcionado proveedor, credenciales ni contrato de API.

## 2. Hardware confirmado y restricciones

- ESP32 DevKit V1 con módulo ESP-WROOM-32, ESP32 clásico. No asumir ESP32-S3, C3 ni PSRAM disponible.
- Pantalla TFT IPS **3.2 pulgadas**, resolución nativa **240 × 320**, interfaz SPI, touch capacitivo por I2C y ranura microSD.
- El empaque indica **ILI9341**. No configurar ST7796 ni resolución 320 × 480: el título del anuncio mezclaba variantes.
- Serigrafía de la pantalla: `3.2" TFT SPI 240X320 V1.0 Capacitive Touch`.
- Touch operativo con el controlador software **FT6336** del ejemplo del fabricante. No asegurar un sufijo concreto del chip sin leer su identificación.
- La familia LCDWiki MSP3222/MSP3223 sirve de referencia documental; no tomar el nombre comercial exacto como identificación física demostrada.
- PCB personalizada fabricada y verificada físicamente. ESP32 soldado directamente; pantalla montada en socket. No cambiar pines para facilitar software ni asumir que hay cables Dupont disponibles.
- Alimentación cableada; GND común. Pantalla alimentada a 5 V en pin 1.
- Diseño mecánico previsto: pantalla delante y ESP32 detrás, atornillado dentro del case. La orientación visual definitiva queda por verificar en el montaje.
- La numeración se obtiene de VCC = 1 y SD_CS = 14, no de “arriba/abajo”: al girar el módulo cambia la posición física, no la numeración.

## 3. Pinout definitivo del PCB

Los números de GPIO son los usados en el código, **no posiciones físicas de la hilera del DevKit**.

| Pin de pantalla | Señal | Conexión al ESP32 | Función |
| --- | --- | --- | --- |
| 1 | VCC | Alimentación de 5 V | Alimentación del módulo |
| 2 | GND | GND común | Referencia eléctrica |
| 3 | LCD_CS | GPIO18 | Selección LCD, activa en bajo |
| 4 | LCD_RST | GPIO19 | Reset LCD, activo en bajo |
| 5 | LCD_RS / DC | GPIO21 | Comando = bajo; datos = alto |
| 6 | SDI / MOSI | GPIO22 | Datos SPI hacia LCD/SD |
| 7 | SCK | GPIO23 | Reloj SPI |
| 8 | LED | GPIO32 | Control de backlight; alto en la prueba |
| 9 | SDO / MISO | GPIO33 | Datos SPI desde LCD/SD |
| 10 | CTP_SCL | GPIO25 | Reloj I2C del touch |
| 11 | CTP_RST | GPIO26 | Reset del touch |
| 12 | CTP_SDA | GPIO27 | Datos I2C del touch |
| 13 | CTP_INT | GPIO14 | Interrupción del touch; prueba por sondeo |
| 14 | SD_CS | GPIO13 | Selección microSD; mantener alta si no se utiliza |

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

## 4. Estado de las pruebas

### Pantalla

La prueba SPI directa basada en la inicialización LCDWiki consiguió mostrar imagen. La base utilizada es `SPIClass busLCD(HSPI)` con pines personalizados:

```cpp
busLCD.begin(LCD_SCK, LCD_MISO, LCD_MOSI, -1);
// En cada transacción:
SPISettings(1000000, MSBFIRST, SPI_MODE0);
```

Frecuencia de referencia probada: **1 MHz**. No sustituirla inmediatamente por 40 u 80 MHz. Una vez conservada la prueba funcional, evaluar velocidades mayores por etapas y registrar cuál funciona en el PCB real.

La prueba usa colores RGB565 y comandos 0x2A (columnas), 0x2B (filas) y 0x2C (escritura de píxeles). Enviar el byte alto antes del bajo. Una fila de 240 píxeles ocupa 480 bytes.

El sketch original alternaba rojo, verde, azul, blanco y negro con esperas de 1500 ms. Esas esperas son de diagnóstico; no deben pasar a la aplicación definitiva. A 1 MHz, enviar todos los píxeles de una pantalla requiere aproximadamente 1.23 s solo en datos; por eso se necesitan actualizaciones parciales.

### Touch

Se obtuvieron coordenadas y número de dedos por Serial a 115200 baudios. Ejemplos reales:

```text
X: 150  Y: 142  Dedos: 1
X: 58   Y: 43   Dedos: 1
X: 95   Y: 159  Dedos: 2
X: 92   Y: 254  Dedos: 1
X: 194  Y: 52   Dedos: 1
```

Esto confirma comunicación y lectura táctil; falta comprobar correspondencia de esquinas, rotación, límites y comportamiento de botones. Ver dos dedos no demuestra un fallo si no se sabe cómo se tocó la pantalla.

Configuración de referencia:

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

La prueba de sondeo no requiere una ISR para CTP_INT, pero el pin está conectado y debe conservarse como GPIO14.

### Nota eléctrica registrada; no bloquear el desarrollo por ella

Mediciones reportadas con GND común: rail de 3V3 = 3.3 V; con touch funcionando, pin 10 ≈ 3.7 V y pin 12 ≈ 3.8 V. Con pantalla sola alimentada externamente se habían reportado ≈ 5.3 V y ≈ 4.8 V respectivamente. Son lecturas de multímetro, no capturas de osciloscopio.

Se informó al usuario de que esos niveles en las señales pueden comprometer las entradas del ESP32. No se ha demostrado daño ni se ha confirmado el mecanismo de la caída de tensión. Morgan decidió continuar con el hardware actual para el proyecto y acepta el riesgo. **No proponer repetidamente modificar SMD, rehacer el PCB o cambiar resistencias como condición para desarrollar el firmware.** Registrar la limitación y considerarla en futuros diagnósticos; el software no corrige el nivel eléctrico.

## 5. Librerías y archivos

| Elemento | Estado y uso |
| --- | --- |
| Arduino core para ESP32 | Entorno utilizado; versión exacta de la prueba no registrada. Identificarla antes de fijar dependencias. |
| `Arduino.h` | API básica del core. |
| `SPI.h` | Biblioteca incluida en el core; usada por el driver LCD directo. No es un archivo que haya que descargar del vendedor. |
| `Wire.h` | I2C del core, utilizado por FT6336. |
| `FT6336.h` y `FT6336.cpp` | Driver local aportado por el usuario; adjuntarlo al proyecto y conservar su procedencia. |
| `touch.h` | Adaptador del ejemplo del vendedor; no es necesario si se instancia FT6336 directamente. Tiene pines que NO coinciden con el PCB. |
| `TFT_eSPI` | Aparece en un ejemplo del vendedor; no es dependencia del sketch SPI directo que se está usando como base. Migración opcional, no probada aquí. |
| `LCDWIKI_SPI`, `LCDWIKI_TOUCH` | Mencionadas en comentarios de ejemplos; no inferir que estén instaladas o utilizadas en la prueba actual. |
| `spi_dev.h` | Auxiliar del demo original con acceso a registros; no se necesita en el sketch que usa SPIClass. |
| `WiFi.h`, `esp_now.h` | Incorporaciones propuestas para la comunicación del HUB, suministradas por el entorno ESP32. |
| `Preferences.h` | Propuesta para persistir configuración en NVS. |

No hay una versión de librería gráfica validada para la aplicación definitiva ni un firmware completo del HUB ya implementado.

### Archivos que acompañarán el desarrollo

Entregar a Codex este documento, el sketch `.ino` exacto que funciona y los archivos `FT6336.h` y `FT6336.cpp`. Si no están en su carpeta de trabajo, pedirlos; no suponer acceso a esta conversación o a sus adjuntos. `touch.h` puede entregarse como referencia, pero no copiar su configuración sin corregirla.

Este documento es un traspaso de contexto, no un paquete de firmware compilable. El nombre del sketch combinado final y su versión de core no quedaron registrados.

### Hallazgos al leer los archivos FT6336 aportados

- Constructor: `FT6336(uint8_t sda, uint8_t scl, uint8_t intPin, uint8_t rst, uint16_t width, uint16_t height)`.
- Dirección predeterminada 0x38; `begin()` devuelve `void`, no un resultado de éxito. Comprobar presencia por I2C y errores de lectura explícitamente.
- Propiedades públicas: `isTouched`, `touches`, `points[2]`; cada punto tiene `x`, `y`, `id`, `size`.
- Si los archivos están junto al sketch, usar `#include "FT6336.h"`, también dentro de `FT6336.cpp`.
- El `touch.h` original define SCL25, SDA32, INT−1 y RST33. **No utilizarlo así**: el PCB exige SDA27, INT14 y RST26.
- El constructor usa `uint8_t` para los pines. No pasar −1: se convierte en 255 y el driver no implementa tratamiento de pin ausente.
- El driver original no valida suficientemente `Wire.endTransmission()`, la cantidad retornada por `requestFrom()` ni los bytes disponibles. Mejorar ese manejo después de guardar la base funcional; no reutilizar coordenadas obsoletas ante un error.
- Las fórmulas de rotación del driver usan `width - x` y `height - y`; revisar los límites de base cero al habilitar otra rotación. La orientación normal es la base inicial.
- En una UI de botones, una pulsación sostenida debe producir una sola acción. Propuesta: activar al soltar dentro del mismo botón; cancelar si el dedo se desplaza fuera. Para la primera versión, ignorar acciones con más de un dedo.

## 6. Inicialización LCD que se debe conservar

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

No atribuir todos los fallos anteriores únicamente a una librería: se cambiaron inicialización y parámetros de prueba. Preservar primero lo comprobado.

## 7. Aplicación definitiva propuesta

### Primera entrega funcional

Una interfaz local en español, con botones grandes para dedo y textos legibles. Comenzar en orientación vertical 240 × 320; decidir horizontal solo después de verificar el case y transformar pantalla/touch conjuntamente.

Pantallas previstas:

1. **Inicio:** estado del sistema, botón de armado/desarmado, puerta abierta/cerrada, movimiento y cantidad de nodos disponibles. Mostrar estados desconocidos como tales.
2. **Nodos:** nombre, tipo, conectado/sin comunicación, último estado y tiempo desde el último mensaje.
3. **Eventos:** lista acotada de aperturas, movimiento, solicitudes RFID, armado/desarmado y pérdida/recuperación de comunicación.
4. **Diagnóstico:** MAC del HUB, canal de radio, estado I2C/touch, contadores de mensajes, errores y versión de firmware.

El HUB será la autoridad del estado global. El RFID enviará una solicitud; el HUB decidirá y publicará el estado resultante. Evitar que una retransmisión de un comando “toggle” arme y desarme varias veces.

### Estados y comportamiento propuestos

| Estado | Comportamiento |
| --- | --- |
| DESARMADO | Recibir y mostrar sensores, registrar eventos; no disparar alarma por movimiento/apertura. |
| SALIDA | Cuenta regresiva opcional antes del armado; valor configurable, inicialmente 10 s para demo. |
| ARMADO | Evaluar eventos de sensores habilitados. |
| ALARMA | Mostrar alerta persistente con origen; permanecer hasta desarmado autorizado. |

La pérdida de comunicación es un estado de salud del nodo, separado del estado de alarma. Propuesta inicial: advertir y registrar, sin disparar automáticamente alarma. Tampoco inventar sirena o buzzer: no se ha definido ese hardware en el HUB.

Políticas iniciales propuestas: iniciar desarmado tras un reinicio, marcar sensores desconocidos hasta recibir estado y no completar el armado con puerta abierta o sensores obligatorios sin comunicación. Hacer estas políticas configurables y documentarlas. Si se desea comportamiento distinto, acordarlo antes de la entrega final.

Autorización: para la demo, botón local explícito y RFID autorizado. Definir después si se necesita PIN local y dónde se conserva la lista de tarjetas; no tratar el UID de una tarjeta como autenticación robusta por sí solo.

## 8. Relación con los otros módulos

| Nodo externo | Información que espera el HUB | Respuesta del HUB |
| --- | --- | --- |
| Puerta / MC-38 | Abierta/cerrada, identificación y estado de conexión | Estado global y confirmaciones necesarias |
| PIR | Movimiento activo/inactivo y estado de conexión | Estado global para su indicador |
| RFID / RC522 | Solicitud de armado/desarmado con identificador de petición y autorización definida | Resultado y estado global confirmado para LEDs |
| Actuador de demostración | Estado y confirmación de órdenes, si se incorpora | Orden explícita, sin mezclarla con el transporte |

No modificar los pinouts ni reimplementar los otros módulos dentro del trabajo inicial del HUB. Los sketches actuales de esos nodos son pruebas locales; no se ha establecido un protocolo ESP-NOW común. Será necesario adaptar sus mensajes al contrato que se acuerde.

## 9. Comunicación ESP-NOW propuesta

Topología en estrella: nodos ↔ HUB. Cada nodo tendrá ID lógico, tipo y MAC registrada. Las MAC reales y el canal aún no están proporcionados; no inventarlos.

Mantener mensajes pequeños (objetivo ≤ 250 bytes), versión de protocolo y serialización explícita. No transmitir `String`, punteros ni estructuras C++ dependientes de padding sin un formato definido.

Contrato inicial a concretar en `protocol.h`:

| Campo | Propósito |
| --- | --- |
| magic + version | Rechazar tramas de otro proyecto o formato |
| messageType | HELLO, STATUS, EVENT, COMMAND, ACK, STATE_SYNC |
| nodeId + nodeType | Identificación lógica, contrastada con MAC permitida |
| bootId + sequence | Distinguir reinicios y detectar duplicados |
| payloadLength | Validar tamaño antes de leer datos |
| payload | Estado del sensor, orden o estado global |

Los ACK deben referenciar bootId/secuencia de la petición. Propuesta: heartbeat cada 5 s, marcar offline tras 15 s sin mensajes válidos; ACK de aplicación y hasta tres intentos para órdenes/eventos críticos. Son parámetros de diseño ajustables, no mediciones de rendimiento.

Reenviar el mismo resultado si llega una petición RFID duplicada, sin ejecutar otra vez el cambio. Preferir órdenes explícitas `ARM` / `DISARM`; si se conserva una solicitud de alternar, deduplicarla antes de decidir. Cada cambio global tendrá una revisión de estado para sincronizar nodos y LEDs tras reconexiones.

Restricciones técnicas a respetar: los dispositivos deben trabajar en el mismo canal. La confirmación de envío de ESP-NOW no prueba que la aplicación haya procesado el mensaje. Los callbacks de radio deben copiar/enqueuear datos y terminar rápidamente; procesar lógica y dibujado fuera de ellos. Fuente: documentación oficial Espressif enlazada al final.

Validar longitud, versión y remitente antes de actuar. Para órdenes de armado/desarmado, prever unicast cifrado y claves fuera del repositorio. No considerar una lista de MAC como autenticación criptográfica.

Si después se añade Wi-Fi con internet para notificaciones, diseñar la convivencia de canal con el AP; no asumir que ESP-NOW puede permanecer en un canal independiente. El funcionamiento local no debe depender del servicio de mensajería.

## 10. Arquitectura del firmware propuesta

Empezar con Arduino C++ y una capa gráfica ligera sobre el driver funcional. Para texto y primitivas se puede evaluar un adaptador de Adafruit_GFX conservando la inicialización propia; es una dependencia nueva opcional y requiere validación. No migrar directamente a LVGL o TFT_eSPI sin mantener una prueba de referencia.

| Archivo o componente propuesto | Responsabilidad |
| --- | --- |
| `hub.ino` o `main.cpp` | Inicialización y coordinación |
| `board_pins.h` | Única definición del pinout |
| `display_driver.*` | SPI, inicialización ILI9341, ventanas y bloques RGB565 |
| `touch_input.*` + `FT6336.*` | Lectura, errores I2C, transformación de coordenadas y eventos táctiles |
| `ui.*` | Vistas, botones y redibujado de regiones modificadas |
| `hub_state.*` | Máquina de estados y decisiones de armado/alarma |
| `protocol.h` | Formato común de mensajes, sin dependencias de UI |
| `espnow_transport.*` | Peers, callbacks, colas, envío y reintentos |
| `node_registry.*` | Estado, identidad y disponibilidad de nodos |
| `event_log.*` | Historial circular acotado en RAM |
| `settings.*` | Preferencias persistentes y valores predeterminados |

No usar `delay()` largos en ejecución normal. Temporizar con `millis()` y diferencias sin signo. Objetivo inicial: leer touch cada 20–30 ms y redibujar solo lo cambiado. No ejecutar I2C, SPI, dibujado ni notificaciones HTTP dentro de callbacks de radio.

No reservar un framebuffer completo por defecto: una pantalla RGB565 ocupa 153600 bytes. Usar bloques/filas pequeños y evitar sprites grandes hasta medir memoria libre. Acotar colas e historial; registrar desbordamientos. Escribir NVS solo cuando cambie configuración, no cada vuelta del bucle.

MicroSD fuera de la primera entrega; conservar CS en alto. No hay RTC ni hora de internet configurados: registrar tiempo desde el arranque y sesión de arranque, sin inventar fechas para los eventos.

Fijar versiones exactas del core y de dependencias cuando se confirme la compilación. Revisar firmas de callbacks ESP-NOW contra esa versión instalada; no copiar ejemplos de versiones incompatibles.

## 11. Orden de implementación y criterios de aceptación

1. **Congelar base:** guardar sketch funcional y FT6336, identificar placa/core y documentar cómo compilar y cargar. Probar colores y lectura táctil sin cambiar pines.
2. **Validar entrada:** prueba de cuatro esquinas y centro; una pulsación sostenida genera una sola acción; errores I2C no disparan botones.
3. **UI local:** inicio, estados y botones; usar eventos simulados identificados claramente como demo. Medir respuesta táctil evitando repintados completos continuos.
4. **Transporte:** mostrar MAC, configurar canal/peers y recibir estado de un nodo real. Descartar tramas inválidas sin bloquearse.
5. **Puerta y PIR:** reflejar cambios, detectar desconexión/reconexión y diferenciar sensor desconocido de estado normal.
6. **RFID:** sincronizar armado/desarmado y LEDs por estado confirmado. Repetir la misma petición no debe alternar dos veces.
7. **Integración:** verificar sensor activo estando desarmado, disparo estando armado, cancelación, reinicio del HUB y reinicio de un nodo. Probar mensajes perdidos, duplicados y recuperación.
8. **Entrega:** código organizado, dependencias fijadas, instrucciones, contrato del protocolo, configuración de ejemplo sin secretos y limitaciones registradas. Notificaciones externas quedan para una etapa posterior.

No declarar estas pruebas aprobadas antes de ejecutarlas en hardware. Si Codex solo puede compilar, distinguir compilación correcta de funcionamiento físico comprobado por Morgan.

## 12. Datos pendientes que no se deben inventar

- Archivo exacto del sketch combinado que está cargado actualmente y versión del core ESP32.
- MAC del HUB y de cada nodo; canal de radio común.
- Orientación final del case y correspondencia de coordenadas con la pantalla.
- Nombre/ID de los nodos y configuración real de mensajes de sus firmwares.
- Política definitiva de armado, tiempo de salida, permiso de armado con fallos y autorización local/RFID.
- Proveedor y credenciales de notificaciones, solo cuando se trabaje en esa etapa.

Estos pendientes no impiden construir la UI local ni organizar el código conservando la base funcional.

## 13. Referencias para continuar

- [LCDWiki: pantalla 3.2 pulgadas ILI9341](https://www.lcdwiki.com/3.2inch_IPS_SPI_Module_ILI9341).
- [Instrucciones del demo ESP32 MSP3222/MSP3223](https://www.lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_MSP3222_MSP3223_ESP32_Demo_Instructions_EN.pdf).
- [Esquema del módulo MSP3222/MSP3223](https://www.lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_Module_MSP3222_MSP3223_Schematic.pdf).
- [Manual del módulo MSP3222/MSP3223](https://www.lcdwiki.com/res/MSP3222_MSP3223/3.2inch_SPI_Module_MSP3222_MSP3223_User_Manual_EN.pdf).
- [Espressif: ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html). Seleccionar la documentación correspondiente al entorno instalado al implementar.

Las fuentes del fabricante son referencias; el pinout definitivo de este documento corresponde al PCB real y prevalece sobre los pines de sus ejemplos.

## 14. Instrucción breve para iniciar en Codex

> Lee este documento y los archivos del proyecto. Conserva el pinout y la inicialización ILI9341 que ya funcionan. Primero identifica el entorno y guarda una prueba de referencia; después implementa por etapas la UI local y el HUB ESP-NOW. Separa hechos comprobados de propuestas. No cambies el hardware ni los pines. No copies la configuración original de touch.h. Si falta el sketch funcional o los archivos FT6336, solicítalos antes de sustituir su comportamiento. Avanza con la UI y la arquitectura aunque todavía no estén disponibles las MAC de los nodos. Entrega código, instrucciones y pruebas realizadas, indicando qué requiere comprobación física.
