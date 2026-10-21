# Contrato de aplicación ESP-NOW v1 — hub, puerta, PIR y RFID (biblioteca 1.2.0)

Extensión vigente RFID: [RFID_CONTRACT.md](RFID_CONTRACT.md). Define tipos 8..12, UID, registro persistente y solicitudes de uso único. Sus decisiones actualizan las limitaciones históricas de esta base. Puerta/PIR conservan su formato y reglas; máximo de trama pasa a 36 bytes. Hub y PIR ya tienen implementación de radio al revisar el 20 de octubre; las instrucciones de su ampliación abajo se conservan como antecedente.

Estado: el usuario confirma comunicación hub–puerta y LED con armado real funcionando. El código actual contiene `SystemController` real, transporte multipeer del hub e integración PIR. RFID dispone ahora de contrato compartido; su firmware y gestión de tarjetas en el hub siguen pendientes. La revisión de código no valida físicamente estas integraciones. Los pines siguen siendo los del contexto general. Se conserva la recuperación automática de canal.

## Integración

Biblioteca Arduino local `DomoticaProtocol` 1.2.0, cabecera `src/DomoticaProtocol.h` y helpers RFID en `src/RfidContract.h`, sin dependencias de Arduino ni radio. Incluir `<DomoticaProtocol.h>` y compilar agregando `--library "D:\Proyecto Domotica\firmware\common"`; no copiar la cabecera a cada firmware. Entorno encontrado: Arduino-ESP32 3.3.10. Cada extremo debe comprobar las firmas de callbacks contra ese core.

### Identidad y seguridad

- IDs fijos: hub=1, puerta=2, PIR=3, RFID=4. Cada ID se vincula a su propia MAC **STA** configurada localmente y LMK por pareja. Las MAC no se inventan ni se aprenden de mensajes entrantes. Los nodos deben tener MAC distintas; sus contadores pueden coincidir sin representar el mismo mensaje.
- Unicast cifrado bidireccional: peers preconfigurados, interfaz STA, `encrypt=true`, LMK de 16 bytes compartida por pareja y PMK explícita. Claves fuera del repositorio. Sin MAC/LMK válidas, el peer afectado queda deshabilitado; una PMK global inválida impide iniciar el transporte. UI/Wi-Fi pueden continuar. No degradar silenciosamente a texto claro.
- Validar MAC origen/destino contra configuración y marco recibido; luego versión, tipo, longitud, IDs, campos, orden y correlación. El codec no autentica por sí mismo. No hay broadcast ni emparejamiento automático. Solo CardRequest puede solicitar una acción RFID, según su permiso; los reportes de sensores nunca son órdenes.

## Formato binario

Enteros sin signo, little-endian. No transmitir structs nativos. Longitud exacta: 17 bytes para reportes de sensores y 30 para STATE_SYNC; tipos RFID de 16, 29 o 36 bytes según RFID_CONTRACT.md. Rechazar bytes extra, campos inválidos y versiones desconocidas, sin actualizar estado ni temporizadores. La versión de aplicación no es la versión ESP-NOW de Espressif.

| Offset | Bytes | Campo |
| --- | --- | --- |
| 0 | 2 | ASCII `DM` |
| 2 | 1 | Versión = 1 |
| 3 | 1 | HELLO=1, STATUS=2, EVENT=3, STATE_SYNC=4, PIR_HELLO=5, PIR_STATUS=6, PIR_EVENT=7 |
| 4 | 1 | ID origen |
| 5 | 1 | ID destino |
| 6 | 4 | Sesión de arranque del emisor |
| 10 | 4 | Secuencia del emisor |
| 14 | 2 | Longitud del payload: 1 o 14 |
| 16 | variable | Payload |

HELLO/STATUS/EVENT: puerta → hub, un byte de estado: UNKNOWN=0, CLOSED=1, OPEN=2. HELLO anuncia arranque/búsqueda/reconexión; STATUS es heartbeat; EVENT informa un cambio estable. Todos contienen una instantánea completa. No representan una solicitud de alarma. La correspondencia eléctrica abierto/cerrado se verifica en la puerta real; filtrado inicial de 40 ms, configurable.

PIR_HELLO/PIR_STATUS/PIR_EVENT: PIR → hub, un byte `PirState` separado de `DoorState`: UNKNOWN=0, NOT_READY=1, NO_MOTION=2, MOTION=3. Misma cabecera y tamaño de reporte (17 bytes); la combinación tipo/origen es estricta. HELLO/STATUS/EVENT con origen PIR, o tipos PIR con origen puerta, son inválidos.

STATE_SYNC: hub → puerta o PIR (destino explícito 2 o 3). Payload: sesión del nodo destinatario confirmada (4), secuencia de ese nodo confirmada (4), revisión del estado global (4), estado global (1), canal real del hub (1). Estados: UNKNOWN=0, DISARMED=1, EXIT_DELAY=2, ARMED=3, ALARM=4. UNKNOWN requiere revisión 0; estados conocidos requieren revisión distinta de 0. Canal representable 1..14, limitado en operación por la configuración regional de ambos extremos. La MAC destino debe corresponder al ID destino; no enviar una respuesta para PIR a puerta ni viceversa.

STATE_SYNC cumple también la función de ACK de aplicación: confirma que el reporte fue procesado. No se necesita un mensaje ACK adicional en esta etapa. El éxito de `esp_now_send` no sustituye esta confirmación. La revisión cambia solamente cuando cambia el estado global y se interpreta dentro de la sesión del hub.

## Sesiones, orden y entrega

- Cada dispositivo incrementa y guarda en NVS su contador local de arranque **antes** de enviar. Empieza en 1; nunca usa 0. No escribir NVS por mensaje. Si falla la persistencia, no habilitar este transporte. Tras borrado/restauración de NVS se requiere reprovisionar y reiniciar ambos extremos para establecer una nueva base; no aceptar automáticamente un contador inferior.
- Secuencia única por emisor para todos sus tipos, empezando en 1 por sesión. Cada mensaje lógico nuevo incrementa la secuencia; retransmitir usa exactamente los mismos bytes. Contadores saltan de UINT32_MAX a 1. Comparación modular con distancia menor a 2^31; distancia exactamente 2^31 se rechaza. Supone menos de 2^31 avances entre observaciones.
- `PeerTracker` conserva último par sesión/secuencia durante una desconexión. Primera trama válida o sesión más nueva establece la base; secuencia nueva actualiza estado. Sesiones antiguas y secuencias atrasadas se descartan. No hace falta recibir HELLO antes de STATUS/EVENT: así se recupera un hub reiniciado o un HELLO perdido.
- Duplicado del último reporte: el hub no vuelve a registrar ni aplicar el evento y no refresca su temporizador de puerta; sí envía un STATE_SYNC nuevo confirmando el mismo reporte. Un duplicado atrasado respecto de otro reporte ya aplicado se descarta. Un cambio de sesión sustituye el estado previo y se registra como reinicio, no como una apertura nueva deducida.
- Puerta mantiene un reporte pendiente como máximo. Reintenta a los 250 y 500 ms, tres transmisiones en total; a los 750 ms sin ACK marca ese envío fallido. Esperar también la finalización del envío ESP-NOW antes de iniciar otro; un callback ausente se trata como fallo del transporte, nunca permite transmisiones simultáneas.
- Mientras hay un pendiente, encolar hasta 8 cambios estables con su instantánea. No encolar heartbeats redundantes. Si se desborda la cola, contar la pérdida, descartar cambios pendientes antiguos y conservar la instantánea más reciente; no afirmar que el historial es completo. Tras fallo de entrega continuar con la siguiente instantánea/heartbeat. Las pérdidas intermedias no se recuperan como historial garantizado.
- Puerta solo acepta STATE_SYNC que referencia exactamente su reporte pendiente o su último reporte confirmado en esta sesión. Aplicar además el orden del emisor hub; mensajes viejos no revierten estado ni refrescan enlace. Solo una referencia al pendiente lo confirma. El hub emite una nueva secuencia en cada respuesta y cada sincronización periódica.

## Heartbeat, disponibilidad y estado global

- Puerta envía STATUS cada 5 s aunque no cambie el sensor. Hub responde a cada reporte aceptado y emite STATE_SYNC cada 5 s para una puerta ya conocida, referenciando su último reporte procesado. Enviar también al cambiar el estado global. Sin reportes recibidos todavía, el hub no fabrica una referencia para STATE_SYNC.
- Tras 15 s sin una trama **nueva, válida y admitida** del otro extremo, marcarlo offline. Comparar `millis()` por resta sin signo. Paquetes inválidos, duplicados y atrasados no sostienen disponibilidad. La recuperación exige datos frescos. No vaciar `PeerTracker` al vencer el timeout.
- En el hub separar `doorState` de `online`: al perder comunicación mostrar estado actual UNKNOWN/Offline y conservar, si se desea, el último dato como histórico. No interpretar desconexión como CLOSED ni como alarma automática.
- LED puerta encendido solo con hub online y estado global ARMED o ALARM; apagado al arrancar, en UNKNOWN, DISARMED, EXIT_DELAY y al vencer timeout. Un ACK de radio no demuestra conexión de aplicación.
- El hub ya tiene autoridad real de armado: publicar `SystemController::state()` y `revision()` a ambos nodos. No volver a UNKNOWN por la incorporación del PIR. UNKNOWN/revisión 0 queda reservado para ausencia real de autoridad; nunca conectar simulaciones a estado de producción. El LED PIR sigue exactamente `ledOn(system, hubOnline)`, independientemente del movimiento o calentamiento; indica armado/enlace, no disponibilidad del sensor.

## Canal Wi-Fi y recuperación automática

- El servicio de red del hub será el único dueño de las decisiones Wi-Fi/canal. No cambiar canal mientras está asociado, conectando o escaneando. Conectado al AP, ESP-NOW usa ese canal. Configurar peers con `channel=0` (canal actual), no un segundo canal fijo. La puerta usa STA sin conectarse a un AP.
- Guardar el último canal del AP cuando cambie. Arranque del hub: iniciar STA y radio local en último canal válido o fallback configurable 1; iniciar un intento de asociación de hasta 10 s. Si falla, cancelar la asociación sin apagar STA, esperar que termine y restaurar el canal local. Nunca esperar internet para iniciar ESP-NOW.
- Sin AP, permitir intentos Wi-Fi acotados de hasta 3 s cada 30 s, cancelando/restaurando el canal local al fallar. Suspender el auto-reconnect independiente del core para evitar escaneos sin control. No reinicializar ESP-NOW ni borrar peers en cada intento. El hub ya separa el control Wi-Fi en `wifi_control.*`; conservar esa integración al añadir el segundo peer.
- Perder internet conservando asociación al AP no modifica el canal. Perder el AP conserva el último canal salvo las ventanas de búsqueda; una reconexión a otro canal lo sustituye. Durante escaneos/asociación puede haber pérdidas de paquetes: no prometer continuidad sin ninguna interrupción; ACK, heartbeat y resincronización cubren la recuperación.
- Puerta arranca buscando primero el último canal que funcionó o fallback 1. Sin respuesta correlacionada, recorre la lista configurada de canales legales. Predeterminado conservador: 1..11; si el AP usa otro canal legal, configurar la lista antes de las pruebas. No inferir la región solo de una trama.
- Cada canal tiene una ventana de 1000 ms y un HELLO nuevo con hasta tres intentos. Cambiar de canal solo después de concluir envíos pendientes. Fijar canal al recibir STATE_SYNC válido/correlacionado cuya información de canal coincida con el canal local. Guardar el canal solo cuando cambie. Si no encuentra hub, repetir el recorrido indefinidamente sin bloquear el sensor.
- Tras timeout del hub, apagar LED y volver a búsqueda. Al comenzar búsqueda descartar la cola de reportes antiguos y transmitir la instantánea actual; contar cambios perdidos. No cambiar de canal solo porque llegó un número en un payload. El hub responde al HELLO en su propio canal; no sigue a la puerta.

## Responsabilidades de las conversaciones

- **Hub:** conservar integración puerta y controlador real; añadir PIR conforme a las instrucciones de ampliación más abajo. Conservar pantalla, touch, reloj y clima.
- **Puerta:** GPIO27 INPUT_PULLUP y LED GPIO16; filtrar y verificar polaridad; gestionar sesión/secuencias, cola/reintentos, búsqueda de canal y enlace; aplicar solo estado confirmado del hub.
- Ambos: colas RX de 8 elementos con copia de MAC/datos/metadatos necesarios y contador de desbordamiento. Callbacks breves: sin UI, I2C, SPI, HTTP ni escritura NVS. Procesar fuera del callback. Diagnósticos sin claves: MAC STA, canal, sesión, secuencia, ACK, rechazos, pérdidas y tiempo desde última recepción válida.
- Conservar la configuración de puerta que ya funciona. Para integrar PIR faltan sus datos locales confirmados: MAC STA, LMK por pareja, PMK local explícita, canales permitidos y configuración física del sensor/LED. Su ausencia no impide compilar el contrato.

## Ampliación PIR: semántica, compatibilidad y actualización

### Estado del sensor y reportes

- `PirState::Unknown`: configuración sin confirmar, polaridad/tiempos no definidos, o lectura no confiable. No significa ausencia de movimiento ni implica desconexión.
- `PirState::NotReady`: configuración válida pero calentamiento o primera estabilización todavía en curso. El nodo puede estar online y recibir ACK/estado global. No usar NO_MOTION durante esta fase.
- `PirState::NoMotion` / `Motion`: lectura válida y estable después del calentamiento. `pirReady()` solo devuelve true para estos dos estados. Readiness y disponibilidad de radio se mantienen separadas.
- `PirHello` transmite la instantánea actual al arrancar, buscar canal o resincronizar; no esperar a que termine el calentamiento para contactar al hub. `PirStatus` publica la instantánea cada 5 s. `PirEvent` se emite al cambiar la instantánea, incluida la transición de NOT_READY al primer estado válido, aunque ese estado inicial sea MOTION. Un nivel activo sostenido no genera EVENT en cada iteración.
- Las reglas anteriores de sesiones, orden, ACK, colas, reintentos, heartbeat, timeout y canal descritas para puerta se aplican también a PIR, sustituyendo sus tipos y estado; no cambian los valores temporales. Un PIR desconocido/no listo se confirma como reporte recibido: el ACK no certifica que el sensor esté listo. No añade comandos, alarmas ni cambios en la política actual de armado.
- Hub usa un `PeerTracker` y estado por MAC/ID: mensajes de un nodo no mantienen online al otro. Tras timeout, mostrar PIR Offline/Unknown; conservar el último dato solo como histórico. Nunca convertir offline, UNKNOWN o NOT_READY a NO_MOTION.
- PIR admite STATE_SYNC solo de la MAC hub autorizada, origen Hub, destino Pir, canal correcto, referencia a su pendiente/confirmado y orden fresco. Usar `correlates(frame, pending, confirmed, Node::Pir)` **antes** de actualizar el tracker. La sobrecarga existente de tres argumentos sigue limitada a Door. Coincidir solo en sesión/secuencia no basta entre dos nodos.
- Hub conserva una sesión y un contador de secuencia global para sus mensajes a ambos nodos; los huecos entre secuencias son válidos. Mantiene referencia pendiente, último sync y revisión publicada **por destinatario**. STATE_SYNC contiene la revisión global actual y la referencia del destinatario, nunca la del otro sensor.

### Compatibilidad

- Biblioteca 1.1.0, versión binaria `kVersion=1`: extensión aditiva con tipos antes no admitidos (5..7) e ID previamente reservado (3). No se cambian valores, longitudes, endian, bytes, estados, tiempos ni cifrado de puerta. `Frame.pir` se añade al final del objeto en memoria y no se serializa para puerta. No existe compatibilidad ABI de structs, que nunca se transmiten.
- `isReport()` conserva su significado **solo puerta**, porque `DoorLink::admit()` lo utiliza como barrera de entrada. Nuevos helpers: `isPirReport()`, `isSensorReport()`, `isSensorNode()`, `pirReady()`. No ampliar `isReport()` globalmente para evitar que el hub trate movimiento como puerta.
- Un hub antiguo con biblioteca 1.0.0 rechaza los tipos PIR. Una puerta antigua rechaza STATE_SYNC destinado a PIR. Puerta 1.0.0 y hub actualizado siguen interoperando: no requiere actualizar puerta, borrar NVS, cambiar su LMK ni reiniciar sus contadores. Recompilar puerta con 1.1.0 conserva su API y la correlación dirigida a Door.
- Actualizar solo la biblioteca **no integra PIR**: `radio_service.cpp` actual admite una MAC y mantiene un único envío/estado de sincronización para puerta. No provisionar ni activar PIR hasta terminar la integración multipeer del hub.
- No hay negociación automática de capacidades: un PIR adelantado encontrará un hub antiguo que no responde y permanecerá buscando/LED apagado. El rollback del hub a puerta-only mantiene puerta; PIR quedará offline hasta recuperar un hub compatible, sin interpretarse como cerrado/sin movimiento.

### Orden e instrucciones para la conversación del hub

1. Leer este contrato y usar la biblioteca 1.1.0. Preservar `SystemController`, sesiones NVS, peer/MAC/LMK puerta, LED real, Wi-Fi, UI, reloj y clima. No imponer PIR como condición nueva de armado ni añadir alarmas en esta entrega.
2. Añadir configuración opcional MAC STA PIR y LMK propia; conservar la PMK existente del hub. Credenciales PIR ausentes/inválidas deshabilitan solo PIR y generan diagnóstico, nunca todo el enlace puerta. Validar MACs únicas, unicast y distintas de la del hub. Registrar peer cifrado STA, canal 0; nunca usar broadcast ni bajar a texto claro.
3. Enrutar primero por MAC provisionada, luego comprobar ID/tipo/destino. Mantener `DoorLink` para tipos 1..3 y añadir `PirLink` para 5..7 con `PirState`, tracker y diagnóstico independientes. No enrutar únicamente por el ID declarado en el payload.
4. Sustituir el único `pendingReference`, `lastSync` y `publishedRevision` por datos por peer. Conservar una sola transmisión ESP-NOW en vuelo y programar turnos equitativos entre pendientes para evitar que PIR prive a puerta de ACK/heartbeat. Copiar identidad destinataria en el registro TX/callback, sin trabajo pesado dentro del callback.
5. Responder a reportes frescos/duplicados actuales con STATE_SYNC **unicast al mismo nodo**, con su referencia, estado real y revisión actual. Cada nodo conocido recibe sync cada 5 s y tras cambio global; emitir nueva secuencia global por respuesta. Un sync a PIR no cuenta como sync publicado a puerta.
6. UI en inglés: `Motion detected`, `No motion`, `Sensor not ready`, `Unknown`, `Online`/`Offline`. Solo cambiar la vista/registro del nodo correspondiente. Primero validar regresión de puerta con PIR deshabilitado; después tráfico simultáneo, secuencias iguales entre nodos, caída/reinicio de uno sin afectar al otro, armado/desarmado y cambio de canal. Cargar el hub solo cuando el usuario lo solicite en esa conversación.

### Instrucciones para la conversación del PIR (después del hub)

1. Conservar diagnóstico original y GPIO27 entrada PIR/GPIO25 LED del contexto general. La lectura local actual solo tiene `Observation::Unknown/NoMotion/Motion`: añadir un adaptador explícito, sin cast numérico. Configuración no confirmada → `PirState::Unknown`; configurado pero `!sensor.ready` → `NotReady`; una vez listo, mapear NoMotion/Motion a sus enums homónimos. No reutilizar `DoorState` ni el pull-up/antirrebote del MC-38.
2. Mantener calentamiento/filtro/polaridades/modo físico configurables y confirmarlos en el hardware; no sustituir los valores no configurados por suposiciones. Usar `Frame.source=Node::Pir`, destino Hub, `PirHello/PirStatus/PirEvent` y `Frame.pir`. La radio puede funcionar antes de readiness si sus credenciales sí están configuradas.
3. Añadir transporte cifrado con MAC hub configurada, LMK específica PIR y PMK local explícita; sesiones NVS propias, una trama pendiente, cola acotada, reintentos de bytes idénticos y búsqueda automática según este contrato. No copiar credenciales ni namespace de sesión de puerta.
4. Validar destinatario y usar la correlación de cuatro argumentos con `Node::Pir`. El LED depende exclusivamente de estado global confirmado y enlace vigente, no de `Motion` ni de readiness. No asumir estado por éxito del envío MAC. Ante timeout se apaga y comienza búsqueda conservando el tracker; publicar snapshot actual al recuperar.
5. Compilar y probar primero calentamiento/not-ready, estado inicial alto y nivel sostenido sin eventos repetidos. Luego integrar contra hub actualizado: ACK cruzado rechazado, duplicados, reinicios, pérdida de hub, recuperación de canal y LED con armado/desarmado real. No modificar common o hub desde esa conversación sin coordinar.

Esta tarea no modifica ni carga hub, puerta o PIR. Actualización prevista: contrato → integración/validación hub manteniendo puerta → integración/validación PIR. La validación física del enlace PIR queda pendiente.

## Validación

Pruebas C++ de evaluación constante en `tests/protocol_tests.cpp`: vector binario conocido, todos los tipos/estados, buffers y longitudes inválidas, versión/identidad/campos, orden/reinicio/wrap, timeout y LED. Se pueden verificar con un compilador C++14 o posterior usando `-fsyntax-only`; los `static_assert` ejecutan la lógica durante compilación. No verifican el driver de radio.

Validación realizada el 19 de octubre de 2026: todas las pruebas de evaluación constante pasaron con el compilador Xtensa instalado (`-std=c++14 -Wall -Wextra -Werror -pedantic -fsyntax-only`). Un sketch mínimo que importa la biblioteca y usa encode/decode/PeerTracker compiló y enlazó con Arduino-ESP32 3.3.10 y FQBN `esp32:esp32:esp32`. Esto verifica la biblioteca, no la integración del hub/puerta ni el enlace físico.

Validación de la ampliación PIR (19 de octubre de 2026): seis suites de evaluación constante pasaron con C++14/Xtensa, warnings como errores: contrato original, extensión PIR, DoorLink del hub, controlador real del hub, lógica de puerta y lectura local PIR. Además se compiló una comparación contra una copia del codec 1.0.0 anterior a esta edición: bytes idénticos y decodificación cruzada para todos los tipos/estados de puerta, rechazo de los tipos PIR y de STATE_SYNC a PIR por el codec antiguo. Se verificaron hashes de 643 archivos de hub/puerta/PIR sin cambios. No se compiló ni cargó un firmware de radio PIR integrado en esta tarea.

Regresión física a ejecutar al integrar PIR: arranque de cada extremo en ambos órdenes; puerta y movimiento simultáneos; calentamiento PIR; pérdida/duplicación de reportes y ACK; reinicios; desconexión de 15 s; arranque sin AP; pérdida solo de internet; reconexión del AP en otro canal; interacción táctil y actualizaciones de reloj/clima durante tráfico. La prueba existente hub–puerta/LED fue confirmada por el usuario; no equivale a aprobar estos escenarios nuevos. Registrar separadamente compilación y resultados sobre PCB.

Referencia: [Espressif ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_now.html), para unicast cifrado, peers/canal actual, callbacks y diferencia entre confirmación MAC y ACK de aplicación. Los tiempos y decisiones de este contrato son propios del proyecto.
