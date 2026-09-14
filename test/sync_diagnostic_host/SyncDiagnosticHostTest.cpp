#include <cassert>
#include <cstring>

#include "SyncDiagnosticHost.h"

int main() {
  char host[96];
  copySyncDiagnosticHost(host, sizeof(host), "https://user:secret@example.com:443/path?token=secret#fragment");
  assert(strcmp(host, "example.com:443") == 0);
  copySyncDiagnosticHost(host, sizeof(host), "http://[::1]:8080/progress");
  assert(strcmp(host, "[::1]:8080") == 0);
  copySyncDiagnosticHost(host, sizeof(host), "example.com/path@secret");
  assert(strcmp(host, "example.com") == 0);
  copySyncDiagnosticHost(host, sizeof(host), "");
  assert(host[0] == '\0');
  copySyncDiagnosticHost(host, 4, "https://example.com");
  assert(strcmp(host, "exa") == 0);
  copySyncDiagnosticHost(host, 1, "example.com");
  assert(host[0] == '\0');
  host[0] = 'x';
  copySyncDiagnosticHost(host, 0, "example.com");
  assert(host[0] == 'x');
  copySyncDiagnosticHost(host, sizeof(host), "https://example\n.com");
  assert(strcmp(host, "example?.com") == 0);
}
