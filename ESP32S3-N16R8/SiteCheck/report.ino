// report.ino — the daily digest, and the identical on-demand STATUS reply.
// Current state only: no uptime percentages, no history.

#include "common.h"

String buildStatusReport() {
  if (hostCount == 0) return String(F("No hosts configured."));

  String out;
  out.reserve(hostCount * 48);

  for (uint8_t i = 0; i < hostCount; i++) {
    const Host &h = hosts[i];
    out += h.name;
    if (h.type == TYPE_PING) {  // the name alone rarely identifies an IP
      out += " (";
      out += h.target;
      out += ')';
    }
    out += ": ";
    out += stateName(h.state);
    out += '\n';
    yield();
  }

  return out;
}

void sendDigest() {
  Serial.println(F("[digest] sending"));

  uint8_t down = 0;
  for (uint8_t i = 0; i < hostCount; i++) {
    if (hosts[i].state == STATE_DOWN) down++;
  }

  // ntfy headers must stay ASCII, so no fancy dashes here.
  String title = F("Daily status");
  if (down) title += " - " + String(down) + " down";

  ntfyPublish(buildStatusReport(), title.c_str(),
              down ? "warning" : "white_check_mark", down ? 3 : 2);
}
