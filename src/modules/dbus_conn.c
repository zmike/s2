#include "S2.h"
#include <Ecore.h>
#include <Eldbus.h>
#include <assert.h>

#define S2_METHOD_BASE "org.s2"

#define STRING_SAFETY(X) X ?: ""

# define E_LIST_HANDLER_APPEND(list, type, callback, data) \
  do \
    { \
       Ecore_Event_Handler *_eh; \
       _eh = ecore_event_handler_add(type, (Ecore_Event_Handler_Cb)callback, data); \
       assert(_eh); \
       list = eina_list_append(list, _eh); \
    } \
  while (0)


static Eina_List *handlers = NULL;
static Eina_Hash *connection_ifaces = NULL;
static Eldbus_Connection *dbus_conn = NULL;
static Eldbus_Service_Interface *core_iface = NULL;

typedef enum
{
   CORE_SIGNAL_STATE
} Core_Signals;

typedef enum
{
   CONNECTION_SIGNAL_STATUS,
   CONNECTION_SIGNAL_MESSAGE_RECEIVED,
   CONNECTION_SIGNAL_MESSAGE_SENT
} Connection_Signals;

static const Eldbus_Signal core_signals[] =
{
   [CORE_SIGNAL_STATE] = {"ConnectionState", ELDBUS_ARGS({"s", "JID"}, {"u", "connection state"}), 0},
   {NULL, NULL, 0}
};

static const Eldbus_Signal connection_signals[] =
{
   [CONNECTION_SIGNAL_STATUS] = {"ContactStatusChange",
    ELDBUS_ARGS({"s", "JID"}, {"s", "photo sha1"}, {"s", "status description"},
                {"i", "priority"}, {"u", "idle"}, {"d", "timestamp"},
                {"u", "Shotgun_User_Status"}, {"u", "Shotgun_Presence_Type"}), 0},
   [CONNECTION_SIGNAL_MESSAGE_RECEIVED] = {"MessageReceived",
    ELDBUS_ARGS({"s", "jid"}, {"s", "message"}, {"u", "Shotgun_Message_Status"}), 0},
   [CONNECTION_SIGNAL_MESSAGE_SENT] = {"MessageSent",
    ELDBUS_ARGS({"s", "jid"}, {"s", "message"}, {"u", "Shotgun_Message_Status"}), 0},
   {NULL, NULL, 0}
};

static void
_dbus_connection_signal_message_sent(S2_Auth *sa, const char *to, const char *msg, Shotgun_Message_Status status, Eina_Bool xhtml_im EINA_UNUSED)
{
   Eldbus_Service_Interface *iface;

   iface = eina_hash_find(connection_ifaces, shotgun_jid_full_get(sa->auth));
   if (iface)
     eldbus_service_signal_emit(iface, CONNECTION_SIGNAL_MESSAGE_SENT, to, msg, status);
}

static Eina_Bool
_dbus_connection_signal_status_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Event_Presence *ev)
{
   Eldbus_Service_Interface *iface;
   Eina_Bool vcard = ev->vcard;

   iface = eina_hash_find(connection_ifaces, shotgun_jid_full_get(ev->account));
   if (iface)
     eldbus_service_signal_emit(iface, CONNECTION_SIGNAL_STATUS, ev->jid, STRING_SAFETY(ev->photo),
                                STRING_SAFETY(ev->description), ev->priority, ev->idle, ev->timestamp,
                                ev->status, ev->type, vcard);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_dbus_connection_signal_message_received_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Event_Message *ev)
{
   Eldbus_Service_Interface *iface;

   iface = eina_hash_find(connection_ifaces, shotgun_jid_full_get(ev->account));
   if (iface)
     eldbus_service_signal_emit(iface, CONNECTION_SIGNAL_MESSAGE_RECEIVED, ev->jid, STRING_SAFETY(ev->msg), ev->status);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_dbus_core_signal_state_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Auth *auth)
{
   eldbus_service_signal_emit(core_iface, CORE_SIGNAL_STATE, shotgun_jid_full_get(auth), shotgun_connection_state_get(auth));
   return ECORE_CALLBACK_RENEW;
}

static void
_dbus_request_name_cb(void *data EINA_UNUSED, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   unsigned int flag;

   if (eldbus_message_error_get(msg, NULL, NULL))
     fprintf(stderr, "Could not request bus name\n");
   else if (!eldbus_message_arguments_get(msg, "u", &flag))
     fprintf(stderr, "Could not get arguments on on_name_request\n");
   else if (!(flag & ELDBUS_NAME_REQUEST_REPLY_PRIMARY_OWNER))
     fprintf(stderr, "Name already in use\n");

}

static Eldbus_Message *
_dbus_quit_cb(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   ecore_main_loop_quit();
   return eldbus_message_method_return_new(msg);
}

static Eldbus_Message *
_dbus_core_connect_cb(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   const char *jid;

   if (!eldbus_message_arguments_get(msg, "s", &jid)) goto error;
   /* FIXME: check available accounts and connect here */
error:
   return eldbus_message_error_new(msg, "org.s2.Core.Invalid", "Account specified was invalid or not found!");
}

static Eldbus_Message *
_dbus_core_disconnect_cb(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   const char *jid;
   S2_Auth *sa;

   if (!eldbus_message_arguments_get(msg, "s", &jid)) goto error;
   sa = ui_shotgun_find(jid);
   if (!sa) goto error;
   shotgun_disconnect(sa->auth);
   return eldbus_message_method_return_new(msg);

error:
   return eldbus_message_error_new(msg, "org.s2.Core.Invalid", "Account specified was invalid or not found!");
}

static Eldbus_Message *
_dbus_connection_sendmessage_cb(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg)
{
   const char *path, *jid, *txt, *p;
   Shotgun_Message_Status st;
   unsigned int id;
   S2_Auth *sa;
   char buf[4096] = {0};
   Eldbus_Message *reply;

   path = eldbus_message_path_get(msg);
   if ((!path) || (!path[0]) || (!path[1])) goto acct_error;
   path++;
   errno = 0;
   id = strtoul(path, NULL, 10);
   if (errno) goto acct_error;
   sa = ui_shotgun_find_by_id(id);
   if (!sa) goto acct_error;

   if (!eldbus_message_arguments_get(msg, "ssu", &jid, &txt, &st)) goto contact_error;

   /* FIXME: need to check existence of resource... */
   p = strchr(jid, '/');
   if (p)
     memcpy(buf, jid, p - jid);
   if (!ui_contact_find(sa, p ? buf : jid)) goto contact_error;
   /* FIXME: need to check xhtml_im */
   ui_shotgun_message_send(sa, jid, txt, st, 0);
   reply = eldbus_message_method_return_new(msg);
   return reply;
acct_error:
   return eldbus_message_error_new(msg, "org.s2.Connection.Invalid", "Account specified was invalid or not found!");
contact_error:
   return eldbus_message_error_new(msg, "org.s2.Connection.NotFound", "No contact with specified JID was found!");
}

static const Eldbus_Method base_methods[] =
{
      { "Quit", NULL, NULL, _dbus_quit_cb, 0},
      {NULL, NULL, NULL, NULL, 0}
};

static const Eldbus_Method core_methods[] =
{
      { "Connect", ELDBUS_ARGS({"s", "JID"}), ELDBUS_ARGS({"b", "success"}), _dbus_core_connect_cb, 0},
      { "Disconnect", ELDBUS_ARGS({"s", "JID"}), ELDBUS_ARGS({"b", "success"}), _dbus_core_disconnect_cb, 0},
      {NULL, NULL, NULL, NULL, 0}
};

static const Eldbus_Method connection_methods[] =
{
      { "SendMessage", ELDBUS_ARGS({"s", "JID"}, {"s", "message text"}, {"u", "Shotgun_Message_Status"}),
        NULL, _dbus_connection_sendmessage_cb, 0},
      {NULL, NULL, NULL, NULL, 0}
};

static const Eldbus_Service_Interface_Desc base_desc =
{
   S2_METHOD_BASE, base_methods, NULL, NULL, NULL, NULL
};

static const Eldbus_Service_Interface_Desc core_desc =
{
   S2_METHOD_BASE ".Core", core_methods, core_signals, NULL, NULL, NULL
};

static const Eldbus_Service_Interface_Desc connection_desc =
{
   S2_METHOD_BASE ".Connection", connection_methods, connection_signals, NULL, NULL, NULL
};

static Eina_Bool
_dbus_connect_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Auth *auth)
{
   if (!eina_hash_find(connection_ifaces, shotgun_jid_full_get(auth)))
     {
        Eldbus_Service_Interface *iface;
        S2_Auth *sa;
        char buf[64];

        sa = shotgun_data_get(auth);
        if (!sa) return ECORE_CALLBACK_RENEW;
        snprintf(buf, sizeof(buf), "/%u", sa->id);
        iface = eldbus_service_interface_register(dbus_conn, buf, &connection_desc);
        eina_hash_add(connection_ifaces, shotgun_jid_full_get(auth), iface);
     }
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_dbus_disconnect_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Auth *auth)
{
   eina_hash_del_by_key(connection_ifaces, shotgun_jid_full_get(auth));
   return ECORE_CALLBACK_RENEW;
}

EAPI S2_Module_Type
ui_module_type(void)
{
   return S2_MODULE_TYPE_UTIL;
}

EAPI Eina_Bool
ui_module_init(void)
{
   eldbus_init();
   dbus_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SESSION);
   eldbus_name_request(dbus_conn, "org.s2", 0, _dbus_request_name_cb, NULL);
   eldbus_service_interface_register(dbus_conn, "/", &base_desc);
   core_iface = eldbus_service_interface_register(dbus_conn, "/", &core_desc);
   ui_shotgun_message_send_listener_add(_dbus_connection_signal_message_sent);
   connection_ifaces = eina_hash_string_superfast_new((Eina_Free_Cb)eldbus_service_object_unregister);
   E_LIST_HANDLER_APPEND(handlers, SHOTGUN_EVENT_MESSAGE, _dbus_connection_signal_message_received_cb, NULL);
   E_LIST_HANDLER_APPEND(handlers, SHOTGUN_EVENT_PRESENCE, _dbus_connection_signal_status_cb, NULL);
   E_LIST_HANDLER_APPEND(handlers, SHOTGUN_EVENT_CONNECTION_STATE, _dbus_core_signal_state_cb, NULL);
   E_LIST_HANDLER_APPEND(handlers, SHOTGUN_EVENT_CONNECT, _dbus_connect_cb, NULL);
   E_LIST_HANDLER_APPEND(handlers, SHOTGUN_EVENT_DISCONNECT, _dbus_disconnect_cb, NULL);
   return EINA_TRUE;
}

EAPI void
ui_module_shutdown(void)
{
   Ecore_Event_Handler *h;

   EINA_LIST_FREE(handlers, h)
     ecore_event_handler_del(h);
   eina_hash_free(connection_ifaces);
   eldbus_shutdown();
}
