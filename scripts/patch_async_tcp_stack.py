"""Keep measured stack headroom on the no-PSRAM Sunton LCD boards."""
from pathlib import Path
Import("env")
root=Path(env.subst("$PROJECT_LIBDEPS_DIR"))/env.subst("$PIOENV")
old="#define CONFIG_ASYNC_TCP_STACK_SIZE 8192 * 2"
new="""#if APP_SUNTON_PANEL
#if APP_SUNTON_PANEL == 3
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192
#else
#define CONFIG_ASYNC_TCP_STACK_SIZE 6144
#endif
#else
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192 * 2
#endif"""
for source in root.glob("AsyncTCP/src/AsyncTCP.h"):
 original=source.read_text(encoding="utf8")
 text=original
 text=text.replace("#define CONFIG_ASYNC_TCP_STACK_SIZE 12288", "#define CONFIG_ASYNC_TCP_STACK_SIZE 8192")
 if text!=original:source.write_text(text,encoding="utf8")
 previous="""#if APP_SUNTON_PANEL
#define CONFIG_ASYNC_TCP_STACK_SIZE 6144
#else
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192 * 2
#endif"""
 if previous in text:text=text.replace(previous,new,1);source.write_text(text,encoding="utf8")
 if new in text:continue
 if text.count(old)!=1:raise RuntimeError("Unexpected AsyncTCP stack configuration")
 source.write_text(text.replace(old,new),encoding="utf8")

# Keep enough admission room for Stop/Volume to free audio buffers. Large
# handlers still return 503 below 16 KB through application middleware.
# Header containers use throwing STL allocation. Refuse another request before
# a no-PSRAM board exhausts its byte-addressable heap under parallel browsers.
for source in root.glob("ESPAsyncWebServer/src/WebRequest.cpp"):
 original=source.read_text(encoding="utf8")
 text=original
 text=text.replace("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 12288", "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 6144")
 text=text.replace("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 2048))", "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 1024))")
 if text!=original:source.write_text(text,encoding="utf8")
 marker="void AsyncWebServerRequest::_onData(void *buf, size_t len) {"
 guard="""
#if APP_SUNTON_PANEL == 3
  if (_parseState <= PARSE_REQ_HEADERS &&
      (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 6144 ||
       heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 1024)) {
    _parseState = PARSE_REQ_FAIL;
    abort();
    return;
  }
#endif
"""
 if marker+guard not in text:
  if text.count(marker)!=1:raise RuntimeError("Unexpected AsyncWebServer request parser")
  text=text.replace(marker,marker+guard,1)
  text='#if defined(ESP32)\n#include <esp_heap_caps.h>\n#endif\n'+text
  source.write_text(text,encoding="utf8")
for source in root.glob("ESPAsyncWebServer/src/WebServer.cpp"):
 original=source.read_text(encoding="utf8")
 text=original
 text=text.replace("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 16384", "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 8192")
 text=text.replace("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 4096)", "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 2048)")
 if text!=original:source.write_text(text,encoding="utf8")
 old="      AsyncWebServerRequest *r = new AsyncWebServerRequest((AsyncWebServer *)s, c);"
 new="""#if APP_SUNTON_PANEL == 3
      if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 8192 ||
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 2048) {
        c->abort();delete c;return;
      }
#endif
      AsyncWebServerRequest *r = new(std::nothrow) AsyncWebServerRequest((AsyncWebServer *)s, c);"""
 if new not in text:
  if text.count(old)!=1:raise RuntimeError("Unexpected AsyncWebServer connection allocation")
  text=text.replace(old,new,1)
  text='#include <new>\n#if defined(ESP32)\n#include <esp_heap_caps.h>\n#endif\n'+text
  source.write_text(text,encoding="utf8")

for source in root.glob("ESPAsyncWebServer/src/WebResponses.cpp"):
 text=source.read_text(encoding="utf8")
 marker="  _ackedLength += len;\n  size_t space = request->client()->space();"
 bounded=marker+"""
#if APP_SUNTON_PANEL == 3
  // One TCP segment per static response prevents parallel browser asset
  // downloads from occupying every byte of the no-PSRAM heap.
  if (_state == RESPONSE_HEADERS || _state == RESPONSE_CONTENT) {
    const size_t pending = _writtenLength > _ackedLength ? _writtenLength - _ackedLength : 0;
    if (pending >= 1460) return 0;
    space = std::min(space, size_t(1460) - pending);
  }
#endif
"""
 if bounded not in text:
  if text.count(marker)!=1:raise RuntimeError("Unexpected AsyncWebServer response window")
  source.write_text(text.replace(marker,bounded,1),encoding="utf8")
