#ifndef S2_TEMPNAME_H
# define S2_TEMPNAME_H

# ifdef HAVE_CONFIG_H
# include "config.h"
# endif

# ifndef alloca
#  ifdef HAVE_ALLOCA_H
#   include <alloca.h>
#  elif defined __GNUC__
#   define alloca __builtin_alloca
#  elif defined _AIX
#   define alloca __alloca
#  else
#   include <stddef.h>
void *alloca (size_t);
#  endif
# endif

#include <Eina.h>
#include <Shotgun.h>

typedef enum
{
   S2_MODULE_TYPE_AUTH, /**< credential storage */
   S2_MODULE_TYPE_LOGIN, /**< creates connection, login window */
   S2_MODULE_TYPE_LIST, /**< creates list view */
   S2_MODULE_TYPE_CHAT, /**< creates chat view */
   S2_MODULE_TYPE_UTIL, /**< misc functionality */
   S2_MODULE_TYPE_LAST, /**< iterator */
} S2_Module_Type;

typedef struct S2_Auth
{
   EINA_INLIST;
   unsigned int id;
   Shotgun_Auth *auth;
   Eina_Hash *contacts;
   Eina_Bool global_otr : 1;
   Eina_Bool mail_notifications : 1;
} S2_Auth;

typedef struct S2_Contact
{
   S2_Auth *auth;
   Eina_Stringshare *jid;
   Eina_Stringshare *name;
   Eina_List *groups;
   Shotgun_Presence_Type ptype;
   Shotgun_User_Subscription subscription;
   Eina_List *presences;
   void *list_item;
   struct
   {
      const char *full_name;
      struct
        {
           const char *type;
           const char *sha1;
           void *data;
           size_t size;
        } photo;
   } info;
   Eina_Bool subscription_pending : 1;
   Eina_Bool xhtml_im : 1;
} S2_Contact;

typedef void (*S2_Message_Send_Listener_Cb)(S2_Auth *sa, const char *to, const char *msg, Shotgun_Message_Status status, Eina_Bool xhtml_im);

EAPI extern int S2_EVENT_CONTACT_ADD;
EAPI extern int S2_EVENT_CONTACT_UPDATE;
EAPI extern int S2_EVENT_CONTACT_PRESENCE_ADD;
EAPI extern int S2_EVENT_CONTACT_PRESENCE_UPDATE;
EAPI extern int S2_EVENT_CONTACT_PRESENCE_DEL;
EAPI extern int S2_EVENT_CONTACT_DEL;

EAPI S2_Contact *ui_contact_find(S2_Auth *sa, const char *jid);
EAPI S2_Auth *ui_shotgun_find(const char *jid);
EAPI S2_Auth *ui_shotgun_find_by_id(unsigned int id);
EAPI Eina_Bool ui_shotgun_userinfo_eq(S2_Contact *sc, Shotgun_User_Info *info);
EAPI void ui_shotgun_message_send(S2_Auth *sa, const char *to, const char *msg, Shotgun_Message_Status status, Eina_Bool xhtml_im);;
EAPI void ui_shotgun_message_send_listener_add(S2_Message_Send_Listener_Cb cb);
EAPI void ui_shotgun_message_send_listener_del(S2_Message_Send_Listener_Cb cb);

static inline Eina_Stringshare *
ui_contact_name_get(const S2_Contact *sc)
{
   if (!sc) return NULL;
   if (sc->name && sc->name[0])
     return sc->name;
   if (sc->info.full_name && sc->info.full_name[0])
     return sc->info.full_name;
   return sc->jid;
}

#endif
