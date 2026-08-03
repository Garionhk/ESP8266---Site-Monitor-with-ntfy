// commands.ino — the ADD / REMOVE / STATUS protocol read off the command topic.
//
//   ADD|Name1|target1;Name2|target2;...
//   REMOVE|Name1;Name2;...
//   STATUS

#include "common.h"

// --------------------------------------------------------------- helpers ---
static bool isIPv4(const String &s) {
  int octets = 0, value = 0, digits = 0;
  for (size_t i = 0; i <= s.length(); i++) {
    char c = (i < s.length()) ? s[i] : '.';
    if (c >= '0' && c <= '9') {
      if (++digits > 3) return false;
      value = value * 10 + (c - '0');
      if (value > 255) return false;
    } else if (c == '.') {
      if (digits == 0) return false;
      octets++;
      value = 0;
      digits = 0;
    } else {
      return false;
    }
  }
  return octets == 4;
}

int findHost(const char *name) {
  for (uint8_t i = 0; i < hostCount; i++) {
    if (strcasecmp(hosts[i].name, name) == 0) return i;
  }
  return -1;
}

// Applies the target parsing rules from plan.md.
bool parseTarget(const String &input, uint8_t &type, char *out, size_t outLen) {
  String t = input;
  t.trim();
  if (t.length() == 0) return false;

  if (isIPv4(t)) {
    type = TYPE_PING;
  } else if (t.startsWith("http://")) {
    type = TYPE_HTTP;
  } else if (t.startsWith("https://")) {
    type = TYPE_HTTPS;
  } else {
    if (t.indexOf('.') < 0 && t != "localhost") return false;  // not a host
    type = TYPE_HTTPS;
    t = "https://" + t;
  }

  if (t.length() >= outLen) return false;
  strlcpy(out, t.c_str(), outLen);
  return true;
}

static void appendList(String &dst, const String &item) {
  if (dst.length()) dst += F(", ");
  dst += item;
}

// ------------------------------------------------------------------- ADD ---
static void cmdAdd(const String &args, String &reply) {
  String added, exists, full, bad;
  int start = 0;
  bool changed = false;

  while (start <= (int)args.length()) {
    int sep = args.indexOf(';', start);
    String entry = (sep < 0) ? args.substring(start) : args.substring(start, sep);
    start = (sep < 0) ? args.length() + 1 : sep + 1;

    entry.trim();
    if (entry.length() == 0) continue;

    int pipe = entry.indexOf('|');
    if (pipe < 0) {
      appendList(bad, entry);
      continue;
    }

    String name   = entry.substring(0, pipe);
    String target = entry.substring(pipe + 1);
    sanitizeText(name);
    sanitizeText(target);

    if (name.length() == 0 || target.length() == 0) {
      appendList(bad, entry);
      continue;
    }
    if (name.length() >= HOST_NAME_LEN) name = name.substring(0, HOST_NAME_LEN - 1);

    if (findHost(name.c_str()) >= 0) {
      appendList(exists, name);
      continue;
    }
    if (hostCount >= MAX_HOSTS) {
      appendList(full, name);
      continue;
    }

    Host &h = hosts[hostCount];
    memset(&h, 0, sizeof(h));
    if (!parseTarget(target, h.type, h.target, sizeof(h.target))) {
      appendList(bad, name + "|" + target);
      continue;
    }
    strlcpy(h.name, name.c_str(), sizeof(h.name));
    h.state      = STATE_UNKNOWN;  // first check decides, no alert on unknown
    h.lastChange = (uint32_t)time(nullptr);
    hostCount++;
    changed = true;

    appendList(added, name + " (" + h.target + ")");
  }

  if (changed) {
    hostsDirty = true;
    hostsSave();
  }

  if (added.length())  reply += "Added: " + added + "\n";
  if (exists.length()) reply += "Already present: " + exists + "\n";
  if (full.length())   reply += "Skipped, " + String(MAX_HOSTS) +
                                "-host limit reached: " + full + "\n";
  if (bad.length())    reply += "Could not parse: " + bad + "\n";
  if (reply.length() == 0) reply = F("ADD: nothing to do\n");
  reply += String(hostCount) + "/" + String(MAX_HOSTS) + " hosts";
}

// ---------------------------------------------------------------- REMOVE ---
static void cmdRemove(const String &args, String &reply) {
  String removed, unknown;
  int start = 0;
  bool changed = false;

  while (start <= (int)args.length()) {
    int sep = args.indexOf(';', start);
    String name = (sep < 0) ? args.substring(start) : args.substring(start, sep);
    start = (sep < 0) ? args.length() + 1 : sep + 1;

    sanitizeText(name);
    if (name.length() == 0) continue;

    int idx = findHost(name.c_str());
    if (idx < 0) {
      appendList(unknown, name);
      continue;
    }

    appendList(removed, hosts[idx].name);
    for (uint8_t i = idx; i + 1 < hostCount; i++) hosts[i] = hosts[i + 1];
    hostCount--;
    changed = true;
  }

  if (changed) {
    hostsDirty = true;
    hostsSave();
  }

  if (removed.length()) reply += "Removed: " + removed + "\n";
  if (unknown.length()) reply += "Unknown name: " + unknown + "\n";
  if (reply.length() == 0) reply = F("REMOVE: nothing to do\n");
  reply += String(hostCount) + "/" + String(MAX_HOSTS) + " hosts";
}

// ------------------------------------------------------------- dispatcher --
void handleCommand(const String &body, uint32_t msgTime) {
  (void)msgTime;

  String msg = body;
  msg.trim();
  if (msg.length() == 0) return;

  // The verb ends at the first '|' OR the first space, whichever comes first,
  // so both "ADD|Name|target" and the more natural "ADD Name|target" work.
  // Splitting on '|' alone read "ADD FileServer | 1.2.3.4" as the verb
  // "ADD FileServer" and rejected the whole command.
  int cut = -1;
  for (unsigned int i = 0; i < msg.length(); i++) {
    char c = msg[i];
    if (c == '|' || c == ' ' || c == '\t') {
      cut = (int)i;
      break;
    }
  }

  String verb = (cut < 0) ? msg : msg.substring(0, cut);
  String args = (cut < 0) ? String("") : msg.substring(cut + 1);
  verb.trim();
  verb.toUpperCase();

  // When the verb was ended by a space, a '|' may still follow it — keep
  // "ADD | Name | target" working, which is the form already in use.
  args.trim();
  if (args.startsWith("|")) args = args.substring(1);

  Serial.printf("[cmd] %s\n", msg.c_str());

  String reply;
  if (verb == "ADD") {
    cmdAdd(args, reply);
    ntfyPublish(reply, "ADD", "heavy_plus_sign", 2);
  } else if (verb == "REMOVE" || verb == "DEL" || verb == "DELETE") {
    cmdRemove(args, reply);
    ntfyPublish(reply, "REMOVE", "heavy_minus_sign", 2);
  } else if (verb == "STATUS") {
    ntfyPublish(buildStatusReport(), "Status", "clipboard", 2);
  } else {
    reply = "Unknown command: " + verb +
            "\nExpected ADD|Name|target;... , REMOVE|Name;... or STATUS";
    ntfyPublish(reply, "Bad command", "warning", 3);
  }
}
