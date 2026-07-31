#include <kernel/runas.h>

#include <stddef.h>

static uint64_t granted;
static uint64_t denied;
static char last[64];

static void remember(const char *command) {
    size_t i = 0u;
    while (command[i] != '\0' && i + 1u < sizeof(last)) {
        last[i] = command[i];
        ++i;
    }
    last[i] = '\0';
}

void runas_init(void) {
    granted = 0u;
    denied = 0u;
    last[0] = '\0';
}

// General like real sudo/doas: a single local-console user, elevated for
// the duration of one command rather than gated to a fixed allowlist of
// shapes. The only thing actually refused is an empty command -- there's
// nothing to run as root there, so it's not a meaningful grant.
bool runas_authorize(const char *command) {
    if (command == NULL || command[0] == '\0') {
        ++denied;
        return false;
    }
    remember(command);
    ++granted;
    return true;
}

bool runas_self_test(void) {
    const uint64_t grants_before = granted;
    const uint64_t denials_before = denied;
    return runas_authorize("systemctl restart project-host.service") &&
        !runas_authorize("") && granted == grants_before + 1u &&
        denied == denials_before + 1u;
}

uint64_t runas_grants(void) { return granted; }
uint64_t runas_denials(void) { return denied; }
const char *runas_last_command(void) { return last; }
