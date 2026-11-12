// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoMetalPublishV1 -- see AdrenoMetalPublishV1.h for provenance and the
// honest HOST-VERIFIED boundary. Freestanding C11: no libc, no MMIO.

#include "AdrenoMetalPublishV1.h"

static void zero_windows(struct enchilada_publish_window w[4])
{
    for (unsigned i = 0; i < 4u; ++i) {
        w[i].start = 0u;
        w[i].bytes = 0u;
    }
}

static int char_eq(char a, char b)
{
    return a == b;
}

/* Match a bounded, NON-NUL-terminated name against a bounded, NUL-terminated
 * literal. Returns the literal length when equal, 0 otherwise. */
static uint32_t literal_eq(const char *name, uint32_t length, const char *lit)
{
    uint32_t n = 0u;
    while (lit[n] != '\0') {
        ++n;
    }
    if (length != n) {
        return 0u;
    }
    for (uint32_t i = 0; i < n; ++i) {
        if (!char_eq(name[i], lit[i])) {
            return 0u;
        }
    }
    return n;
}

static int is_delimiter(char c)
{
    return c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n';
}

static int is_name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '*';
}

int enchilada_publish_open_type_is_plugin(uint32_t type)
{
    return type == kEnchiladaPublishPluginOpenType;
}

int enchilada_publish_open_type_is_handoff(uint32_t type)
{
    return type == kEnchiladaPublishHandoffOpenType;
}

int enchilada_publish_open_type_is_marker(uint32_t type)
{
    return (type >> 16) == kEnchiladaPublishPluginOpenType && (type & 0xffffu) != 0u;
}

const char *enchilada_publish_open_type_name(uint32_t type)
{
    if (enchilada_publish_open_type_is_marker(type)) {
        return "marker";
    }
    if (enchilada_publish_open_type_is_plugin(type)) {
        return "plugin";
    }
    if (enchilada_publish_open_type_is_handoff(type)) {
        return "handoff";
    }
    return "unsupported";
}

int enchilada_publish_is_render_server(const char *name, uint32_t length)
{
    if (name == 0) {
        return 0;
    }
    return literal_eq(name, length, ENCHILADA_PUBLISH_RENDER_SERVER) != 0u;
}

int enchilada_publish_client_name_valid(const char *name, uint32_t length)
{
    if (name == 0 || length == 0u) {
        return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        if (!is_name_char(name[i])) {
            return 0;
        }
    }
    return 1;
}

/* P2:152-156: strtok_r over " ,\t\r\n"; "*" | exact | trailing-'*' prefix. */
int enchilada_publish_client_listed(const char *list, uint32_t list_length,
                                    const char *self, uint32_t self_length)
{
    if (list == 0 || self == 0 || self_length == 0u) {
        return 0;
    }
    uint32_t i = 0u;
    while (i < list_length) {
        while (i < list_length && is_delimiter(list[i])) {
            ++i;
        }
        uint32_t start = i;
        while (i < list_length && !is_delimiter(list[i])) {
            ++i;
        }
        uint32_t n = i - start;
        if (n == 0u) {
            continue;
        }
        const char *tok = list + start;
        /* "*" names every process. */
        if (n == 1u && tok[0] == '*') {
            return 1;
        }
        /* Trailing-star prefix: "com.apple.WebKit.*" matches a prefix. */
        if (tok[n - 1u] == '*') {
            uint32_t prefix = n - 1u;
            if (prefix <= self_length) {
                uint32_t k = 0u;
                while (k < prefix && char_eq(tok[k], self[k])) {
                    ++k;
                }
                if (k == prefix) {
                    return 1;
                }
            }
            continue;
        }
        /* Exact name. */
        if (n == self_length) {
            uint32_t k = 0u;
            while (k < n && char_eq(tok[k], self[k])) {
                ++k;
            }
            if (k == n) {
                return 1;
            }
        }
    }
    return 0;
}

uint32_t enchilada_publish_client_cap(int is_render_server, uint32_t limit)
{
    if (is_render_server) {
        return limit;
    }
    if (limit != 0u && limit < kEnchiladaPublishClientCap) {
        return limit;
    }
    return kEnchiladaPublishClientCap;
}

uint32_t enchilada_publish_queue_base(uint32_t slot)
{
    return slot * kEnchiladaPublishMaxQueues;
}

void enchilada_publish_windows_a630(struct enchilada_publish_window out[4],
                                    uint32_t *specified_mask)
{
    if (out != 0) {
        zero_windows(out);
    }
    if (specified_mask != 0) {
        *specified_mask = kEnchiladaPublishA630WindowsSpecifiedMask;
    }
}

int enchilada_publish_windows_consistent(const struct enchilada_publish_window w[4],
                                         uint32_t mask)
{
    if (w == 0) {
        return 0;
    }
    if (mask != kEnchiladaPublishA630WindowsSpecifiedMask) {
        return 0;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        if (w[i].start != 0u || w[i].bytes != 0u) {
            return 0;
        }
    }
    return 1;
}

int enchilada_publish_windows_are_board_derived(void)
{
    return 1;
}

int enchilada_publish_sheng_reference(uint32_t slot, uint64_t *start, uint64_t *bytes)
{
    if (start == 0 || bytes == 0 || slot >= kEnchiladaPublishMaxClients) {
        return 0;
    }
    if (slot == 0u) {
        *start = ENCHILADA_PUBLISH_SHENG_SLOT0_START;
        *bytes = ENCHILADA_PUBLISH_SHENG_SLOT0_BYTES;
        return 1;
    }
    if (slot == 1u) {
        *start = ENCHILADA_PUBLISH_SHENG_SLOT1_START;
    } else if (slot == 2u) {
        *start = ENCHILADA_PUBLISH_SHENG_SLOT2_START;
    } else {
        *start = ENCHILADA_PUBLISH_SHENG_SLOT3_START;
    }
    *bytes = ENCHILADA_PUBLISH_SHENG_CLIENT_BYTES;
    return 1;
}

uint32_t enchilada_publish_per_object_cap_bytes_shift(void)
{
    return kEnchiladaPublishPerObjectBytesShift;
}

const char *enchilada_publish_stage_name(uint32_t stage)
{
    switch (stage) {
        case kEnchiladaPublishStageSeedMarkers: return "seed markers";
        case kEnchiladaPublishStageDiscovery: return "discovery";
        case kEnchiladaPublishStagePublishGate: return "publish gate";
        case kEnchiladaPublishStageRenderServerDevice: return "render server device";
        case kEnchiladaPublishStageClientAllowList: return "client allow list";
        case kEnchiladaPublishStageClientDevice: return "client device";
        case kEnchiladaPublishStageKernelUserClient: return "kernel user client";
        case kEnchiladaPublishStageGpuWindowScope: return "gpu window scope";
        case kEnchiladaPublishStageScanoutComposite: return "scanout composite";
        default: return "unknown";
    }
}

const char *enchilada_publish_blocker_name(uint32_t bit)
{
    switch (bit) {
        case kEnchiladaPublishBlockerMacosToolchain: return "macos toolchain";
        case kEnchiladaPublishBlockerGen1Backend: return "gen1 backend";
        case kEnchiladaPublishBlockerSignedZap: return "signed zap";
        case kEnchiladaPublishBlockerSmmuContext: return "smmu context";
        case kEnchiladaPublishBlockerOsAbi: return "os abi";
        case kEnchiladaPublishBlockerBoardVaMap: return "board va map";
        default: return "unknown";
    }
}

uint32_t enchilada_publish_stage_blockers(uint32_t stage)
{
    switch (stage) {
        case kEnchiladaPublishStageSeedMarkers:
            return kEnchiladaPublishBlockerMacosToolchain; /* hdiutil/codesign */
        case kEnchiladaPublishStageDiscovery:
            return kEnchiladaPublishBlockerMacosToolchain | kEnchiladaPublishBlockerOsAbi;
        case kEnchiladaPublishStagePublishGate:
            return kEnchiladaPublishBlockerMacosToolchain | kEnchiladaPublishBlockerOsAbi;
        case kEnchiladaPublishStageRenderServerDevice:
            return kEnchiladaPublishBlockerGen1Backend | kEnchiladaPublishBlockerOsAbi |
                   kEnchiladaPublishBlockerMacosToolchain;
        case kEnchiladaPublishStageClientAllowList:
            return 0u; /* pure policy, portable - the module implements it */
        case kEnchiladaPublishStageClientDevice:
            return kEnchiladaPublishBlockerGen1Backend | kEnchiladaPublishBlockerOsAbi |
                   kEnchiladaPublishBlockerMacosToolchain;
        case kEnchiladaPublishStageKernelUserClient:
            return kEnchiladaPublishBlockerGen1Backend | kEnchiladaPublishBlockerSignedZap |
                   kEnchiladaPublishBlockerSmmuContext | kEnchiladaPublishBlockerOsAbi;
        case kEnchiladaPublishStageGpuWindowScope:
            return kEnchiladaPublishBlockerGen1Backend | kEnchiladaPublishBlockerSmmuContext |
                   kEnchiladaPublishBlockerBoardVaMap;
        case kEnchiladaPublishStageScanoutComposite:
            return kEnchiladaPublishBlockerGen1Backend | kEnchiladaPublishBlockerOsAbi |
                   kEnchiladaPublishBlockerMacosToolchain;
        default:
            return 0u;
    }
}

uint32_t enchilada_publish_establishment_blockers(void)
{
    uint32_t bits = 0u;
    for (uint32_t s = 0; s < kEnchiladaPublishStageCount; ++s) {
        bits |= enchilada_publish_stage_blockers(s);
    }
    return bits;
}

int enchilada_publish_frontend_allowed(int gen1_backend, int compiler,
                                       int signed_zap, int smmu_context,
                                       int os_abi, int macos_toolchain,
                                       int board_va_map)
{
    return gen1_backend && compiler && signed_zap && smmu_context &&
           os_abi && macos_toolchain && board_va_map;
}
