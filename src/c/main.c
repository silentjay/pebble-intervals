#include <pebble.h>

#include "protocol.h"
#include "ui.h"

int main(void) {
  protocol_init();
  protocol_set_commit_callback(ui_on_routines_committed);
  ui_show_menu();
  protocol_request_routines();
  app_event_loop();
  return 0;
}
