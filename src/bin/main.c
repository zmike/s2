#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <Ecore.h>
#include <Ecore_Con.h>
#include "S2.h"

#include <dlfcn.h>
#define DBG(...)            EINA_LOG_DOM_DBG(ui_log_dom, __VA_ARGS__)
#define INF(...)            EINA_LOG_DOM_INFO(ui_log_dom, __VA_ARGS__)
#define WRN(...)            EINA_LOG_DOM_WARN(ui_log_dom, __VA_ARGS__)
#define ERR(...)            EINA_LOG_DOM_ERR(ui_log_dom, __VA_ARGS__)
#define CRI(...)            EINA_LOG_DOM_CRIT(ui_log_dom, __VA_ARGS__)

typedef struct _S2_Module S2_Module;

typedef S2_Module_Type (*S2_Module_Type_Cb)(void);
typedef Eina_Bool (*S2_Module_Init_Cb)(void);
typedef void (*S2_Module_Shutdown_Cb)(void);
typedef int (*S2_Module_Priority_Cb)(void);


struct _S2_Module
{
   EINA_INLIST;
   int priority; //lower = better
   S2_Module_Type type;
   Eina_Module *mod;
   S2_Module_Init_Cb init;
   S2_Module_Shutdown_Cb shutdown;
   Eina_Module *module;
};

int ui_log_dom = -1;
EAPI int S2_EVENT_CONTACT_ADD = -1;
EAPI int S2_EVENT_CONTACT_UPDATE = -1;
EAPI int S2_EVENT_CONTACT_PRESENCE_ADD = -1;
EAPI int S2_EVENT_CONTACT_PRESENCE_UPDATE = -1;
EAPI int S2_EVENT_CONTACT_PRESENCE_DEL = -1;
EAPI int S2_EVENT_CONTACT_DEL = -1;


static Ecore_Event_Handler *dh = NULL;
static Ecore_Event_Handler *ch = NULL;

static Eina_Inlist *connections_list = NULL;
static Eina_Hash *connections = NULL;
static Eina_Hash *connections_by_id = NULL;
static Eina_Hash *contacts = NULL;
static Eina_Inlist *modules[S2_MODULE_TYPE_LAST];

static Eina_List *message_send_listeners = NULL;
static unsigned int connected_count = 0;

static void
fake_free(void *d EINA_UNUSED, void *ev EINA_UNUSED)
{}

static Eina_Bool
module_check(Eina_Module *m, void *d EINA_UNUSED)
{
   const char *filename;
   S2_Module *mod;
   S2_Module_Type type = S2_MODULE_TYPE_LAST;
   S2_Module_Init_Cb cb;
   S2_Module_Type_Cb type_cb;
   S2_Module_Priority_Cb prio_cb;

   filename = eina_module_file_get(m);
   filename = strrchr(filename, '/') + 1;
   if (strcmp(filename + (strlen(filename) - 3), SHARED_LIB_SUFFIX))
     return EINA_FALSE;

   if (!eina_module_load(m))
     {
        ERR("Could not verify module named '%s'. Error: '%s'", filename, eina_error_msg_get(eina_error_get()));
        return EINA_FALSE;
     }

   cb = eina_module_symbol_get(m, "ui_module_init");
   if (!cb) goto error;
   type_cb = eina_module_symbol_get(m, "ui_module_type");
   if (!type_cb) goto error;
   type = type_cb();
   if (type >= S2_MODULE_TYPE_LAST)
     {
        ERR("Unimplemented module type!");
        goto error;
     }
   prio_cb = eina_module_symbol_get(m, "ui_module_priority");

   
   mod = calloc(1, sizeof(S2_Module));
   mod->mod = m;
   mod->type = type;
   if (prio_cb)
     mod->priority = prio_cb();
   mod->init = cb;
   mod->shutdown = eina_module_symbol_get(m, "ui_module_shutdown");
   mod->module = m;
   modules[type] = eina_inlist_append(modules[type], EINA_INLIST_GET(mod));

   return EINA_TRUE;
error:
   eina_module_unload(m);
   return EINA_FALSE;
}

static void
s2_auth_free(S2_Auth *sa)
{
   connections_list = eina_inlist_remove(connections_list, EINA_INLIST_GET(sa));
   eina_hash_del_by_key(connections_by_id, &sa->id);
   eina_hash_free(sa->contacts);
   shotgun_free(sa->auth);
   free(sa);
}

static void
sc_free(S2_Contact *sc)
{
   Shotgun_Event_Presence *p;

   EINA_LIST_FREE(sc->presences, p)
     shotgun_event_presence_free(p);
   eina_stringshare_del(sc->jid);
   free(sc);
}

static void
sc_free_cb(void *d EINA_UNUSED, S2_Contact *sc)
{
   eina_hash_del_by_key(sc->auth->contacts, sc->jid);
}

static Eina_Bool
con_state(void *d EINA_UNUSED, int type EINA_UNUSED, Shotgun_Auth *auth EINA_UNUSED)
{
   INF("Connection state++");
   return ECORE_CALLBACK_RENEW;
}


static Eina_Bool
disc(void *d EINA_UNUSED, int type EINA_UNUSED, Shotgun_Auth *auth)
{
   INF("Disconnected");
   connected_count--;
   if (!connected_count)
     ecore_main_loop_quit();
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
con(void *d EINA_UNUSED, int type EINA_UNUSED, Shotgun_Auth *auth)
{
   INF("Connected!");

   if (!eina_hash_find(connections, shotgun_jid_get(auth)))
     {
        S2_Auth *sa;

        sa = malloc(sizeof(S2_Auth));
        sa->auth = auth;
        shotgun_data_set(auth, sa);
        sa->contacts = eina_hash_string_superfast_new((Eina_Free_Cb)sc_free);
        eina_hash_direct_add(connections, shotgun_jid_get(auth), sa);
        sa->id = eina_hash_population(connections);
        eina_hash_add(connections_by_id, &sa->id, sa);
        connections_list = eina_inlist_append(connections_list, EINA_INLIST_GET(sa));
     }
   connected_count++;
   shotgun_presence_set(auth, SHOTGUN_USER_STATUS_XA, "wat", 1);

   shotgun_iq_server_query(auth);
   shotgun_iq_roster_get(auth);
   shotgun_presence_send(auth);

   return ECORE_CALLBACK_RENEW;
}

static S2_Contact *
sc_add(S2_Auth *sa, const char *jid)
{
   S2_Contact *sc;

   sc = calloc(1, sizeof(S2_Contact));
   sc->auth = sa;
   sc->jid = eina_stringshare_add(jid);
   eina_hash_direct_add(sa->contacts, sc->jid, sc);
   ecore_event_add(S2_EVENT_CONTACT_ADD, sc, fake_free, NULL);
   return sc;
}

static int
sc_presence_compare_cb(Shotgun_Event_Presence *a, Shotgun_Event_Presence *b)
{
   /* highest priority first */
   return b->priority - a->priority;
}

static void
sc_presence_add(S2_Contact *sc, Shotgun_Event_Presence *ev)
{
   Eina_List *l;
   Shotgun_Event_Presence *p;

   EINA_LIST_FOREACH(sc->presences, l, p)
     {
        if (ev->jid == p->jid)
          /* updating an already-existing presence */
          break;
        p = NULL;
     }
   if (p)
     {
        p->priority = ev->priority;
        sc->presences = eina_list_sort(sc->presences, 0, (Eina_Compare_Cb)sc_presence_compare_cb);
        ecore_event_add(S2_EVENT_CONTACT_PRESENCE_UPDATE, sc, fake_free, NULL);
     }
   else
     {
        p = calloc(1, sizeof(Shotgun_Event_Presence));
        p->account = sc->auth->auth;
        p->priority = ev->priority;
        p->jid = eina_stringshare_ref(ev->jid);
        sc->presences = eina_list_sorted_insert(sc->presences, (Eina_Compare_Cb)sc_presence_compare_cb, p);
        ecore_event_add(S2_EVENT_CONTACT_PRESENCE_ADD, sc, fake_free, NULL);
     }

   eina_stringshare_replace(&p->photo, ev->photo);
   eina_stringshare_replace(&p->description, ev->description);
   p->idle = ev->idle;
   p->timestamp = ev->timestamp;
   p->status = ev->status;
   p->type = ev->type;
   p->vcard = ev->vcard;
}

static void
sc_presence_del(S2_Contact *sc, Shotgun_Event_Presence *ev)
{
   Shotgun_Event_Presence *p;
   Eina_List *l;

   EINA_LIST_FOREACH(sc->presences, l, p)
     {
        if (ev->jid != p->jid) continue;
        sc->presences = eina_list_remove_list(sc->presences, l);
        shotgun_event_presence_free(p);
        ecore_event_add(S2_EVENT_CONTACT_PRESENCE_DEL, sc, fake_free, NULL);
        return;
     }
}

static Eina_Bool
presence_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Event_Presence *ev)
{
   S2_Auth *sa;
   S2_Contact *sc;
   const char *base_jid = ev->jid, *p;

   sa = shotgun_data_get(ev->account);
   p = strchr(ev->jid, '/');
   if (p)
     base_jid = strndupa(ev->jid, p - ev->jid);
   sc = ui_contact_find(sa, base_jid);
   if (!sc)
     {
        if (!ev->status)
          {
             /* ...wtf */
             return ECORE_CALLBACK_RENEW;
          }
        if (ev->type != SHOTGUN_PRESENCE_TYPE_SUBSCRIBE)
          {
             if (!strstr(shotgun_jid_get(ev->account), base_jid))
               WRN("Unknown contact '%s'; likely a bug", base_jid);
             return ECORE_CALLBACK_RENEW;
          }
        sc = sc_add(sa, base_jid);
        sc->subscription = SHOTGUN_USER_SUBSCRIPTION_FROM;;
        sc_presence_add(sc, ev);
        return ECORE_CALLBACK_RENEW;
     }
   if (!ev->status)
     {
        /* contact went offline */
        sc_presence_del(sc, ev);
        return ECORE_CALLBACK_RENEW;
     }
   sc_presence_add(sc, ev);
   switch (ev->type)
     {
      case SHOTGUN_PRESENCE_TYPE_SUBSCRIBE:
      case SHOTGUN_PRESENCE_TYPE_UNSUBSCRIBE:
        sc->ptype = ev->type;
      default:
        break;
     }
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
iq_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Event_Iq *ev)
{
   Eina_List *l;
   S2_Contact *sc;
   S2_Auth *sa = shotgun_data_get(ev->account);

   switch (ev->type)
     {
        case SHOTGUN_IQ_EVENT_TYPE_DISCO_QUERY:
        {
           Shotgun_Iq_Disco *iq = ev->ev;
           char *feature;
           int test = 0;

           sc = eina_hash_find(sa->contacts, iq->jid);
           if (!sc) break;
           EINA_LIST_FOREACH(iq->features, l, feature)
             {
                if (!strcmp(feature, "http://jabber.org/protocol/xhtml-im"))
                  test++;
                else if (!strcmp(feature, "urn:xmpp:bob"))
                  test++;
                if (test == 2) break;
             }
           sc->xhtml_im = (test == 2);
           break;
        }
        case SHOTGUN_IQ_EVENT_TYPE_ROSTER:
        {
           Shotgun_User *user;
           Eina_List *ll;
           Eina_Stringshare *g;

           EINA_LIST_FOREACH(ev->ev, l, user)
             {
                if (user->subscription == SHOTGUN_USER_SUBSCRIPTION_REMOVE)
                  {
                     ecore_event_add(S2_EVENT_CONTACT_DEL, eina_hash_find(sa->contacts, user->jid), (Ecore_End_Cb)sc_free_cb, NULL);;
                     continue;
                  }
                sc = eina_hash_find(sa->contacts, user->jid);
                if (sc)
                  ecore_event_add(S2_EVENT_CONTACT_UPDATE, sc, fake_free, NULL);
                else
                  sc = sc_add(sa, user->jid);
                eina_stringshare_replace(&sc->name, user->name);
                EINA_LIST_FREE(sc->groups, g)
                  eina_stringshare_del(g);
                EINA_LIST_FOREACH(user->groups, ll, g)
                  sc->groups = eina_list_append(sc->groups, eina_stringshare_ref(g));
                sc->subscription = user->subscription;
                sc->subscription_pending = user->subscription_pending;
             }
           break;
        }
      case SHOTGUN_IQ_EVENT_TYPE_INFO:
        {
           Shotgun_User_Info *info = ev->ev;

           sc = eina_hash_find(sa->contacts, info->jid);
           if (!sc)
             {
                ERR("WTF!");
                break;
             }
           if (ui_shotgun_userinfo_eq(sc, ev->ev))
             {
                INF("User info for %s unchanged, not updating cache", sc->jid);
                break;
             }
           eina_stringshare_replace(&sc->info.full_name, info->full_name);
           eina_stringshare_replace(&sc->info.photo.type, info->photo.type);
           eina_stringshare_replace(&sc->info.photo.sha1, info->photo.sha1);
           sc->info.photo.size = info->photo.size;
           if (info->photo.data && info->photo.size)
             {
                free(sc->info.photo.data);
                sc->info.photo.data = malloc(info->photo.size);
                memcpy(sc->info.photo.data, info->photo.data, info->photo.size);
             }
           ecore_event_add(S2_EVENT_CONTACT_UPDATE, sc, fake_free, NULL);
           break;
        }
      case SHOTGUN_IQ_EVENT_TYPE_SERVER_QUERY:
        if (shotgun_iq_gsettings_available(sa->auth))
          shotgun_iq_gsettings_query(sa->auth);
        break;
      case SHOTGUN_IQ_EVENT_TYPE_SETTINGS:
        sa->global_otr = shotgun_iq_otr_get(sa->auth);
        sa->mail_notifications = shotgun_iq_gsettings_mailnotify_get(sa->auth);
        if (sa->mail_notifications)
          shotgun_iq_gsettings_mailnotify_ping(sa->auth);
        else if (!shotgun_iq_gsettings_available(sa->auth))
          {
             Eina_Iterator *it;

             it = eina_hash_iterator_data_new(sa->contacts);
             EINA_ITERATOR_FOREACH(it, sc)
               shotgun_iq_disco_info_get(sa->auth, sc->jid);
             eina_iterator_free(it);
          }
        break;
      case SHOTGUN_IQ_EVENT_TYPE_MAILNOTIFY:
        /* TODO: mailnotify somehow... */
        break;
      /* TODO: other IQ event types */
     }
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
message_cb(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Event_Message *ev)
{
#warning send message to chat module...
   return ECORE_CALLBACK_RENEW;
}

/********************************* API ******************************/

EAPI S2_Contact *
ui_contact_find(S2_Auth *sa, const char *jid)
{
   return eina_hash_find(sa->contacts, jid);
}

EAPI void
ui_contact_chat_open(S2_Contact *sc)
{
   
}

EAPI S2_Auth *
ui_shotgun_find(const char *jid)
{
   return eina_hash_find(connections, jid);
}

EAPI S2_Auth *
ui_shotgun_find_by_id(unsigned int id)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(id, NULL);
   return eina_hash_find(connections_by_id, &id);
}

EAPI void
ui_shotgun_message_send(S2_Auth *sa, const char *to, const char *msg, Shotgun_Message_Status status, Eina_Bool xhtml_im)
{
   Eina_List *l;
   S2_Message_Send_Listener_Cb cb;

   shotgun_message_send(sa->auth, to, msg, status, xhtml_im);
   EINA_LIST_FOREACH(message_send_listeners, l, cb)
     cb(sa, to, msg, status, xhtml_im);
}

EAPI void
ui_shotgun_message_send_listener_add(S2_Message_Send_Listener_Cb cb)
{
   EINA_SAFETY_ON_NULL_RETURN(cb);
   message_send_listeners = eina_list_append(message_send_listeners, cb);
}

EAPI void
ui_shotgun_message_send_listener_del(S2_Message_Send_Listener_Cb cb)
{
   EINA_SAFETY_ON_NULL_RETURN(cb);
   message_send_listeners = eina_list_remove(message_send_listeners, cb);
}


EAPI Eina_Bool
ui_shotgun_userinfo_eq(S2_Contact *sc, Shotgun_User_Info *info)
{
   if (sc->info.full_name != info->full_name) return EINA_FALSE;
   if ((!!sc->info.photo.data) != (!!info->photo.data)) return EINA_FALSE;
   return sc->info.photo.sha1 == info->photo.sha1;
}

/*********************** MAIN **********************/

int
main(int argc, char *argv[])
{
   Eina_Array *mods;
   S2_Module *mod;
   const char *modpath;
   Eina_Bool loaded = EINA_FALSE;

   shotgun_init();

   ui_log_dom = eina_log_domain_register("ui", EINA_COLOR_LIGHTRED);
   eina_log_domain_level_set("ui", EINA_LOG_LEVEL_DBG);
   if (!ecore_con_ssl_available_get())
     {
        CRI("SSL support is required in ecore!");
        exit(1);
     }
   //eina_log_domain_level_set("shotgun", EINA_LOG_LEVEL_DBG);
   ch = ecore_event_handler_add(SHOTGUN_EVENT_CONNECT, (Ecore_Event_Handler_Cb)con, NULL);
   dh = ecore_event_handler_add(SHOTGUN_EVENT_DISCONNECT, (Ecore_Event_Handler_Cb)disc, NULL);
   ecore_event_handler_add(SHOTGUN_EVENT_CONNECTION_STATE, (Ecore_Event_Handler_Cb)con_state, NULL);
   ecore_event_handler_add(SHOTGUN_EVENT_PRESENCE, (Ecore_Event_Handler_Cb)presence_cb, NULL);
   ecore_event_handler_add(SHOTGUN_EVENT_IQ, (Ecore_Event_Handler_Cb)iq_cb, NULL);
   ecore_event_handler_add(SHOTGUN_EVENT_MESSAGE, (Ecore_Event_Handler_Cb)message_cb, NULL);

   S2_EVENT_CONTACT_ADD = ecore_event_type_new();
   S2_EVENT_CONTACT_UPDATE = ecore_event_type_new();
   S2_EVENT_CONTACT_PRESENCE_ADD = ecore_event_type_new();
   S2_EVENT_CONTACT_PRESENCE_UPDATE = ecore_event_type_new();
   S2_EVENT_CONTACT_PRESENCE_DEL = ecore_event_type_new();
   S2_EVENT_CONTACT_DEL = ecore_event_type_new();

   ecore_app_args_set(argc, (const char**)argv);

   /* UI_MODULE_DIR can be set by the user to override and use a different module loading directory,
    * such as the build directory
    */
   modpath = getenv("UI_MODULE_DIR");
   mods = eina_module_list_get(NULL, modpath ?: UI_MODULE_PATH, EINA_FALSE, (Eina_Module_Cb)module_check, NULL);
   eina_array_free(mods);

   connections_by_id = eina_hash_int32_new(NULL);
   connections = eina_hash_string_superfast_new((Eina_Free_Cb)s2_auth_free);
   contacts = eina_hash_string_superfast_new(NULL);

   /* do some config loading here to see if we're reconnecting saved accts... */
   EINA_INLIST_FOREACH(modules[S2_MODULE_TYPE_AUTH], mod)
     {
        /* loop all the auth modules in case someone is crazy and saves different accts
         * using different mechanisms...
         */
        if (mod->init())
          {
             /* credentials successfully loaded! */
             loaded = EINA_TRUE;
          }
     }

   if (!loaded)
     {
        /* if no saved credentials and no login mods, exit */
        if (!modules[S2_MODULE_TYPE_LOGIN])
          {
             ERR("No credentials and no login modules :(");
             exit(1);
          }
        EINA_INLIST_FOREACH(modules[S2_MODULE_TYPE_LOGIN], mod)
          {
             /* otherwise load the first login module */
             if (!mod->init())
               {
                  ERR("Module init failed!");
                  exit(1);
               }
             break;
          }
     }
   EINA_INLIST_FOREACH(modules[S2_MODULE_TYPE_UTIL], mod)
     {
        if (!mod->init())
          ERR("Module '%s' failed to load!", eina_module_file_get(mod->mod));
     }

   EINA_INLIST_FOREACH(modules[S2_MODULE_TYPE_LIST], mod)
     {
        if (!mod->init())
          ERR("Module '%s' failed to load!", eina_module_file_get(mod->mod));
     }

   ecore_main_loop_begin();

   return 0;
}
