"""NimBLE 1.4.3 normally deletes an existing bond on any repeat-pair request.
Guardian must require the physical pairing window before allowing that.
"""
from pathlib import Path
Import("env")

if env.subst("$PIOENV") == "LilyGo_TDeck_companion_radio_touch":
    path = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "NimBLE-Arduino/src/NimBLEServer.cpp"
    text = path.read_text(encoding="utf-8")
    marker = "// Guard Mesh: physical permission before replacing a bond."
    needle = "        case BLE_GAP_EVENT_REPEAT_PAIRING: {"
    replacement = needle + '''
            // Guard Mesh: physical permission before replacing a bond.
            extern bool guardMeshAllowRepeatPairing(uint16_t);
            if (!guardMeshAllowRepeatPairing(event->repeat_pairing.conn_handle))
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
'''
    if marker not in text:
        if text.count(needle) != 1:
            raise RuntimeError("Guardian pairing patch context drifted")
        path.write_text(text.replace(needle, replacement), encoding="utf-8")
    elif replacement not in text:
        raise RuntimeError("Guardian pairing patch was modified; refusing an unverified security build")
    text = path.read_text(encoding="utf-8")
    needle = "        case BLE_GAP_EVENT_SUBSCRIBE: {"
    replacement = needle + '''
            // Guard Mesh: CCCD for the raw Guardian Request characteristic.
            extern void guardMeshSubscribe(uint16_t, uint16_t, bool);
            guardMeshSubscribe(event->subscribe.conn_handle, event->subscribe.attr_handle,
                               event->subscribe.cur_notify != 0);
'''
    if "// Guard Mesh: CCCD" not in text:
        if text.count(needle) != 1:
            raise RuntimeError("Guardian CCCD patch context drifted")
        path.write_text(text.replace(needle, replacement), encoding="utf-8")
    elif replacement not in text:
        raise RuntimeError("Guardian CCCD patch was modified")
