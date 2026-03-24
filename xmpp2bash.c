#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strophe.h>
#include <ctype.h>
#include <sys/wait.h>

/**
 * Session context structure to store bot credentials
 * @param jid: Bot's XMPP JID (e.g., user@server.com)
 * @param password: Bot's authentication password
 */
typedef struct {
    const char *jid;
    const char *password;
} xmpp_ctx;

/**
 * Execute system command with arguments and capture output
 * @param command: Command to execute
 * @param args: Array of arguments (NULL-terminated)
 * @return: Dynamically allocated string with command output (must be freed)
 */
static char* execute_bash(const char *msg) {

    if (msg == NULL) {
        fprintf(stderr, "Error: NULL input message\n");
        return NULL;
    }

    size_t cmd_len = strlen("./proxy.sh '") + strlen(msg) + strlen("'") + 1;
    char *cmd = (char*)malloc(cmd_len);
    if (cmd == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for command\n");
        return NULL;
    }

    snprintf(cmd, cmd_len, "./proxy.sh '%s'", msg);

    FILE *pipe = popen(cmd, "r");
    free(cmd);
    if (pipe == NULL) {
        fprintf(stderr, "Error: Failed to open pipe to command\n");
        return NULL;
    }

    char *output = NULL;
    char buffer[1024];
    size_t total_size = 0;

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t line_len = strlen(buffer);
        char *temp = (char*)realloc(output, total_size + line_len + 1);
        if (temp == NULL) {
            fprintf(stderr, "Error: Failed to reallocate memory for output\n");
            free(output);
            pclose(pipe);
            return NULL;
        }
        output = temp;
        strcpy(output + total_size, buffer);
        total_size += line_len;
    }

    int exit_status = pclose(pipe);
    if (exit_status == -1) {
        fprintf(stderr, "Error: Failed to close pipe\n");
        free(output);
        return NULL;
    } else if (WEXITSTATUS(exit_status) != 0) {
        fprintf(stderr, "Warning: Command exited with non-zero status: %d\n", 
                WEXITSTATUS(exit_status));
    }

    if (output == NULL) {
        output = (char*)malloc(1);
        if (output != NULL) {
            output[0] = '\0';
        }
    }

    return output;
}

/**
 * Message handler callback - Core command execution logic
 * Processes incoming chat messages, parses commands, and executes them
 * @param conn: Active XMPP connection
 * @param msg: Incoming message stanza
 * @param userdata: Pointer to session context
 * @return: 1 to keep handler registered, 0 to unregister
 */
static int message_handler(xmpp_conn_t *conn, xmpp_stanza_t *msg, void *userdata) {
    xmpp_stanza_t *body, *reply, *reply_body, *text_node;
    const char *type;
    const char *from;
    xmpp_ctx *ctx = (xmpp_ctx *)userdata;
    char *command_output = NULL;
    char **args = NULL;
    int argc = 0;

    // Get message metadata
    from = xmpp_stanza_get_attribute(msg, "from");
    type = xmpp_stanza_get_attribute(msg, "type");

    // Ignore non-chat messages (group chats, errors, etc.)
    if (!type || strcmp(type, "chat") != 0) {
        return 1;
    }

    // Extract message body content
    body = xmpp_stanza_get_child_by_name(msg, "body");
    if (!body) {
        return 1;
    }

    // Get message text content
    const char *message_text = xmpp_stanza_get_text(body);
    if (!message_text || strlen(message_text) == 0) {
        return 1;
    }

    // Print received message to console
    printf("Received from %s: %s\n", from, message_text);

    command_output = execute_bash(message_text);

    if (!command_output) {
        command_output = strdup("Error: Unknown error occurred");
    }

    xmpp_ctx_t *xmpp_ctx = xmpp_conn_get_context(conn);
    
    // 1. Create main message stanza
    reply = xmpp_message_new(xmpp_ctx, "chat", from, NULL);
    if (reply == NULL) {
        fprintf(stderr, "Failed to create reply stanza\n");
        free(command_output);
        return 1;
    }

    // 2. Create body element
    reply_body = xmpp_stanza_new(xmpp_ctx);
    xmpp_stanza_set_name(reply_body, "body");
    xmpp_stanza_set_ns(reply_body, XMPP_NS_CLIENT); // Critical for older versions

    // 3. Create text node (fixed content injection)
    text_node = xmpp_stanza_new(xmpp_ctx);
    xmpp_stanza_set_text(text_node, command_output);
    xmpp_stanza_add_child(reply_body, text_node);
    xmpp_stanza_release(text_node); // Release after adding to parent

    // 4. Add body to message
    xmpp_stanza_add_child(reply, reply_body);
    xmpp_stanza_release(reply_body); // Release after adding to parent

    // 5. Set bot JID as sender (fixed attribute setting)
    xmpp_stanza_set_attribute(reply, "from", ctx->jid);

    // Send reply
    xmpp_send(conn, reply);
    
    // Cleanup resources
    xmpp_stanza_release(reply);
    free(command_output);

    return 1;
}

/**
 * Connection state handler callback
 * Manages connection lifecycle events
 * @param conn: Active XMPP connection
 * @param event: Connection event type
 * @param error: Error code (0 for success)
 * @param stream_error: Detailed stream error (if any)
 * @param userdata: Pointer to session context
 */
static void conn_handler(xmpp_conn_t *conn, xmpp_conn_event_t event, 
                        int error, xmpp_stream_error_t *stream_error, void *userdata) {
    xmpp_ctx *ctx = (xmpp_ctx *)userdata;
    xmpp_ctx_t *ctx_ptr = xmpp_conn_get_context(conn);
    xmpp_stanza_t *presence;

    switch (event) {
        case XMPP_CONN_CONNECT:
            printf("Successfully connected to XMPP server!\n");
            printf("Bot JID: %s\n", ctx->jid);
            printf("Waiting for messages... (Press Ctrl+C to exit)\n");
            
            // Register message handler for incoming messages
            xmpp_handler_add(conn, message_handler, NULL, "message", NULL, userdata);
            
            // Send online presence status
            presence = xmpp_presence_new(ctx_ptr);
            xmpp_send(conn, presence);
            xmpp_stanza_release(presence);
            break;

        case XMPP_CONN_DISCONNECT:
            printf("Disconnected from server\n");
            xmpp_stop(ctx_ptr);
            break;

        default:
            printf("Connection error occurred: %d\n", error);
            xmpp_stop(ctx_ptr);
            break;
    }
}

/**
 * Main entry point
 * Initializes XMPP library, parses arguments, and starts event loop
 * @param argc: Number of command line arguments
 * @param argv: Command line arguments array
 * @return: 0 on success, non-zero on failure
 */
int main(int argc, char **argv) {
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;
    xmpp_ctx bot_ctx;

    // Validate command line arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <XMPP_JID> <PASSWORD>\n", argv[0]);
        return 1;
    }

    // Initialize session context with provided credentials
    bot_ctx.jid = argv[1];
    bot_ctx.password = argv[2];

    // Initialize XMPP library
    xmpp_initialize();
    ctx = xmpp_ctx_new(NULL, NULL);

    // Create new XMPP connection
    conn = xmpp_conn_new(ctx);
    
    // Set authentication credentials
    xmpp_conn_set_jid(conn, bot_ctx.jid);
    xmpp_conn_set_pass(conn, bot_ctx.password);

    // Enforce TLS encryption for secure connections
    xmpp_conn_set_flags(conn, XMPP_CONN_FLAG_MANDATORY_TLS);

    // Connect to XMPP server
    printf("Connecting as %s...\n", bot_ctx.jid);
    if (xmpp_connect_client(conn, NULL, 0, conn_handler, &bot_ctx) != XMPP_EOK) {
        fprintf(stderr, "Connection failed!\n");
        xmpp_conn_release(conn);
        xmpp_ctx_free(ctx);
        xmpp_shutdown();
        return 1;
    }

    // Start main event loop (blocks until disconnected)
    xmpp_run(ctx);

    // Cleanup resources
    xmpp_conn_release(conn);
    xmpp_ctx_free(ctx);
    xmpp_shutdown();

    return 0;
}