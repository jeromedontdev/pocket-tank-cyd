"""Test actual manifest parser and pre-activation verifier with a native compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

PARSER_TEST = r'''
#undef NDEBUG
#include <assert.h>
static void check(cJSON *root, bool expected) {
    char *text = cJSON_PrintUnformatted(root);
    update_manifest_t m; char url[NET_URL_MAX + 1];
    assert(text);
    assert(update_parse_manifest(text, &m, url, sizeof url) == expected);
    cJSON_free(text);
}
int main(void) {
    const char *valid = "{\"format\":1,\"fork\":\"pocket-tank-cyd\","
        "\"board\":\"" PT_BOARD "\",\"release\":\"2.3.2\",\"min_from\":\"2.3.1\","
        "\"app\":{\"url\":\"https://github.com/example/app.bin\",\"size\":1234,"
        "\"sha256\":\"0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef\"}}";
    cJSON *base = cJSON_Parse(valid);
    check(base, true);
    const char *boards[] = {"cyd2432s028", "elegoo28", "unknown"};
    for (unsigned i=0; i<sizeof boards/sizeof *boards; i++) {
        cJSON *r = cJSON_Duplicate(base, true);
        cJSON_ReplaceItemInObject(r, "board", cJSON_CreateString(boards[i]));
        check(r, !strcmp(boards[i], PT_BOARD)); cJSON_Delete(r);
    }
    update_manifest_t m; char url[NET_URL_MAX + 1];
    assert(update_parse_manifest(valid, &m, url, sizeof url));
    assert(m.sha256[0] == 1 && m.sha256[7] == 0xef && m.sha256[31] == 0xef);
    assert(m.release_num == 0x020302 && m.min_from == 0x020301);
    char short_url[8];
    assert(!update_parse_manifest(valid, &m, short_url, sizeof short_url));
    const char *required[] = {"format","fork","board","release","app"};
    for (unsigned i=0;i<sizeof required/sizeof *required;i++) {
        cJSON *r=cJSON_Duplicate(base,true); cJSON_DeleteItemFromObject(r,required[i]);
        check(r,false); cJSON_Delete(r);
    }
    const char *bad_versions[]={"2.3","2.3.2oops","-1.3.2","256.0.0","0.0.0","2.3.99999999999999999","2. 3.2"};
    for(unsigned i=0;i<sizeof bad_versions/sizeof *bad_versions;i++) {
        cJSON *r=cJSON_Duplicate(base,true); cJSON_ReplaceItemInObject(r,"release",cJSON_CreateString(bad_versions[i]));
        check(r,false); cJSON_Delete(r);
    }
    const char *bad_urls[]={"http://example.com/a","https:///a","https://a@b/a","https://a/a#b","https://a/a b","https://a/\\b","file:///a"};
    for(unsigned i=0;i<sizeof bad_urls/sizeof *bad_urls;i++) {
        cJSON *r=cJSON_Duplicate(base,true); cJSON_ReplaceItemInObject(cJSON_GetObjectItem(r,"app"),"url",cJSON_CreateString(bad_urls[i]));
        check(r,false); cJSON_Delete(r);
    }
    const char *fields[]={"size","sha256","url"};
    for(unsigned i=0;i<3;i++) {
        cJSON *r=cJSON_Duplicate(base,true); cJSON_DeleteItemFromObject(cJSON_GetObjectItem(r,"app"),fields[i]);
        check(r,false); cJSON_Delete(r);
    }
    double sizes[]={-1,0,319,1234.5,1966081,1e100};
    for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++) {
        cJSON *r=cJSON_Duplicate(base,true); cJSON_ReplaceItemInObject(cJSON_GetObjectItem(r,"app"),"size",cJSON_CreateNumber(sizes[i]));
        check(r,false); cJSON_Delete(r);
    }
    const char *bad_hashes[]={"","abc","g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    for(unsigned i=0;i<3;i++) {
        cJSON *r=cJSON_Duplicate(base,true); cJSON_ReplaceItemInObject(cJSON_GetObjectItem(r,"app"),"sha256",cJSON_CreateString(bad_hashes[i]));
        check(r,false); cJSON_Delete(r);
    }
    cJSON_AddNumberToObject(base,"release_num",1); check(base,false);
    cJSON_Delete(base);
    puts("PASS: actual OTA parser requires current board " PT_BOARD ", bounded strict versions, HTTPS URL, exact size and SHA-256");
}
'''

VERIFY_STUBS = r'''
#undef NDEBUG
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "update.h"
#include "version.h"
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define PSA_HASH_OPERATION_INIT {0}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
typedef struct { size_t bytes; uint32_t hash; } psa_hash_operation_t;
typedef struct { int unused; } esp_partition_t;
typedef struct { char version[32]; } esp_app_desc_t;
typedef struct { char magic[8]; char board[24]; } pt_board_marker_t;
static update_manifest_t s_m;
static bool s_abort;
static uint8_t image[2501];
static bool bad_read, bad_hash, bad_setup, bad_finish, wrong_board, wrong_version;
static int esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t n) {
    (void)p;
    if(bad_read) return -1;
    if(offset==PT_BOARD_MARKER_OFFSET && n==sizeof(pt_board_marker_t)) {
        pt_board_marker_t m={PT_BOARD_MAGIC,PT_BOARD};
        if(wrong_board)m.board[0]='x';
        memcpy(out,&m,n); return 0;
    }
    if(offset==32 && n==sizeof(esp_app_desc_t)) {
        esp_app_desc_t d={"2.3.2"};
        if(wrong_version)d.version[0]='9';
        memcpy(out,&d,n); return 0;
    }
    assert(offset+n<=sizeof image);memcpy(out,image+offset,n);return 0;
}
static int psa_crypto_init(void){return 0;}
static int psa_hash_setup(psa_hash_operation_t *h,int algorithm){(void)algorithm;h->bytes=0;h->hash=0;return bad_setup?-1:0;}
static int psa_hash_update(psa_hash_operation_t *h,const uint8_t *p,size_t n){
    for(size_t i=0;i<n;i++)h->hash=h->hash*33+p[i];
    h->bytes+=n;return bad_hash?-1:0;
}
static int psa_hash_finish(psa_hash_operation_t *h,uint8_t *out,size_t cap,size_t *length){
    assert(cap==32);assert(h->bytes==sizeof image);
    for(int i=0;i<32;i++)out[i]=(uint8_t)(h->hash>>(i%4*8));
    *length=32;return bad_finish?-1:0;
}
static void psa_hash_abort(psa_hash_operation_t *h){(void)h;}
'''

VERIFY_TEST = r'''
int main(void){
    esp_partition_t slot={0};s_m.app_size=sizeof image;strcpy(s_m.release,"2.3.2");
    for(size_t i=0;i<sizeof image;i++)image[i]=(uint8_t)i;
    psa_hash_operation_t h=PSA_HASH_OPERATION_INIT;size_t length;
    psa_hash_setup(&h,1);psa_hash_update(&h,image,sizeof image);psa_hash_finish(&h,s_m.sha256,32,&length);
    assert(verify_download(&slot,sizeof image));
    assert(!verify_download(&slot,sizeof image-1));
    assert(!verify_download(&slot,0));
    s_m.sha256[0]^=1;assert(!verify_download(&slot,sizeof image));s_m.sha256[0]^=1;
    bool *failures[]={&bad_read,&bad_hash,&bad_setup,&bad_finish,&wrong_board,&wrong_version,&s_abort};
    for(unsigned i=0;i<sizeof failures/sizeof *failures;i++){
        *failures[i]=true;assert(!verify_download(&slot,sizeof image));*failures[i]=false;
    }
    image[sizeof image-1]^=1;assert(!verify_download(&slot,sizeof image));image[sizeof image-1]^=1;
    assert(verify_download(&slot,sizeof image));
    puts("PASS: actual pre-activation verifier checks every byte including tail, size, board/version, mismatch, cancellation, read and hash failures");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    cjson = ROOT / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    if not cjson.exists():
        cjson = ROOT / ".browser-tools" / "cJSON"
    source = (ROOT / "common" / "update.c").read_text()
    source = source[:source.index("/* the setup's palette")]
    net = (ROOT / "firmware" / "main" / "net_port_esp.c").read_text()
    verify = net[net.index("static bool verify_download("):net.index("static void do_install(")]
    install = net[net.index("static void do_install("):net.index("static void radio_down(")]
    assert install.index("verify_download(") < install.index("esp_https_ota_finish(")
    with tempfile.TemporaryDirectory(prefix="cyd-ota-test-") as folder:
        folder = Path(folder)
        for board, name, text in ((board, name, text) for board in ("cyd", "elegoo")
                                 for name, text in (("manifest", source + PARSER_TEST),
                                                    ("verify", VERIFY_STUBS + verify + VERIFY_TEST))):
            test = folder / (board + "-" + name + ".c")
            test.write_text(text)
            command = [args.cc] + (["cc"] if Path(args.cc).stem.lower() == "zig" else [])
            command += ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "common"), "-I" + str(cjson), str(test)]
            if board == "elegoo":
                command.append("-DPT_BOARD_ELEGOO=1")
            if name == "manifest":
                command += [str(cjson / "cJSON.c")]
            executable = folder / (board + "-" + name + ".exe")
            command += ["-o", str(executable)]
            if __import__("sys").platform != "win32":
                command.append("-lm")
            subprocess.run(command, check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
