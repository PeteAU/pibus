void server_init(int port);
void server_handle_message(const unsigned char *msg, int length);
void server_notify_tx(const unsigned char *msg, int length);
void server_cleanup();
