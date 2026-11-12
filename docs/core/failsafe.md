# Failsafe

A crash in an extension must not take the iPod with it. The panic screen records the crash, the next boot writes the report and disables the extension it attributes the crash to, and a held key starts the core without any extension.

## Panic screen

The exception entries in `core/panic/` capture a checksummed crash record in reserved IRAM and draw the panic screen: the attributed extension when there is one, the path the report will have and a request to open an issue. The screen waits for a fresh Center press before a controlled software restart. Menu + Center is a different reset path and may discard the pending record.

## Crash record

| Field | |
|---|---|
| extension | the full id of the attributed extension, when one is attributed |
| firmware and build ids | |
| registers | saved at the exception |
| stack | a bounded snapshot |

Attribution identifies a possible cause, not proof. The site stubs record their owner so a raw-site handler can be attributed.

## Next boot

Before loading extensions, `core_recovery_prepare` writes `/Revola/Crashes/<report-id>.txt`, verifies the bytes by reading them back and disables the attributed extension. Settings > Extensions allows a manual re-enable, which also clears the crashed mark.

| Case | Behaviour |
|---|---|
| attributed extension | the record is kept until a later boot reads both the report and the disabled flag, or the user re-enables the extension |
| incomplete or conflicting record | preserved; the report is saved under an unused id, which can differ from the one the panic screen showed |
| read error | the pending record stays for a later boot |
| physical reset or power loss | can discard the pending record |

The same-boot readback checks contents, not the durability of the medium.

## Core-only boot

Holding Previous while RevolaOS boots sets `core_loader_core_only`: `core_loader_boot` loads nothing and Settings > Extensions shows "Core only".
