# Project guidance

## Context and confirmed decisions

- The primary reference is `docs/context/Proyecto_Domotica_Contexto_Codex.md`. It takes precedence over the older hub document.
- `docs/context/HUB_Pantalla_Contexto_Codex.md` supplements the primary reference with display and touch details. Its Spanish UI proposal and unspecified notification provider are superseded.
- All visible hub UI text must be in English. WhatsApp notifications use CallMeBot. The project covers four independent ESP32 firmwares: hub, door, PIR, and RFID.
- The hardware is assembled and has passed individual component tests according to the project context. Integrated communication and final firmware are not yet validated. Inspect the current files before assuming implementation progress.

## Conversation boundaries

- Work on one firmware per conversation. Limit changes to the requested module and dependencies necessary for that task; do not modify other firmwares incidentally.
- The general conversation handles project coordination and shared decisions. Documentation work belongs in a separate conversation unless the user explicitly requests it here.
- Do not create new conversations automatically.
- `firmware/common/` is reserved for the shared protocol and reusable components. Coordinate changes affecting multiple modules through the general conversation, explain compatibility impact, and avoid silently changing the contract from a single-module task.

## Read only the relevant context

- For general coordination, consult the primary reference and the files involved in the requested change.
- For hub work, consult the primary reference's hub, system-state, radio, and notification sections as relevant, plus the display context and actual display/touch sources.
- Manufacturer display references are in `references/hub/display/3.2inch_SPI_Module_ILI9341_MSP3222_MSP3223_V1.0/`. Start with the relevant files in `1-Demo/Demo_ESP32/`, including `Install libraries/FT6336-arduino/` for the supplied touch driver; consult specifications, schematics, and manuals only as needed. Do not load the entire package as context.
- Preserve this manufacturer package as reference material. Its examples, pin assignments, library choices, and precompiled firmware do not override the general project context or prove compatibility with the assembled PCB. These examples are not the user's confirmed working diagnostic sketch. Adapt needed code within `firmware/hub/`, preserving the original references.
- For door work, consult the primary reference's hardware constraints, door pinout, shared LED rule, system-state, and radio sections, plus the existing shared contract.
- For PIR work, consult the hardware constraints, PIR pinout and stabilization behavior, shared LED rule, system-state, and radio sections, plus the existing shared contract.
- For RFID work, consult the hardware constraints, RFID pinout, authorization and confirmed-state behavior, system-state, and radio sections, plus the existing shared contract.
- Search for the needed sections rather than loading unrelated hardware designs, histories, or every module's source. Revisit broader context when a change crosses module boundaries.

## Implementation safeguards

- **Pinout authority: use the definitive GPIO tables in `docs/context/Proyecto_Domotica_Contexto_Codex.md`. These are the project's actual PCB connections and override every manufacturer example, `touch.h`, `User_Setup.h`, library default, and demo wiring diagram. Never replace them with vendor pins. If an existing project source contradicts these tables, report the exact conflict before changing code or assuming another pinout.**
- For the hub, the mandatory GPIO assignments are: LCD CS=18, RST=19, DC=21, MOSI=22, SCK=23, MISO=33, BL=32; touch SCL=25, RST=26, SDA=27, INT=14; SD CS=13 (keep high while unused). Check these against the primary context when adapting any example. Do not copy the manufacturer's touch pin configuration.
- Preserve the documented GPIO assignments, physical hardware, functional diagnostic sketches, and working driver initialization. Preserve a diagnostic reference before modifying working code.
- Inspect the actual toolchain and dependency versions. Do not assume that candidate libraries or proposed features are already installed or validated.
- The hub owns global state. Nodes report sensor facts or request actions; their indicators reflect confirmed state and current connectivity.
- Keep local operation independent of internet availability. Do not invent credentials, MAC addresses, authorized cards, or physical test results.
- Distinguish confirmed requirements from proposals still requiring a decision. Report source review, compilation, and physical validation separately; hardware tests require the user's confirmation.

## Folder conventions

- `firmware/{hub,door,pir,rfid}/`: individual firmware sources. `firmware/common/`: shared components.
- `hardware/kicad/`: complete KiCad projects, preserving their internal names, associated exports, and hidden histories.
- `hardware/{hub,door,pir,rfid}/{laser,cnc,enclosure}/`: manufacturing files and case sources/exports by module.
- The STEP alongside the door KiCad project is a PCB 3D export, not an enclosure. Keep it with that project.
- `docs/context/`: existing context documents. Do not generate additional documentation solely to explain the folder reorganization.
- `references/hub/display/`: original manufacturer documentation, example firmware, drivers, and tools for the hub display, kept together with their original internal structure.
