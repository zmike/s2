#include "S2.h"
#include <Elementary.h>
#include <assert.h>

#define WEIGHT evas_object_size_hint_weight_set
#define ALIGN evas_object_size_hint_align_set
#define EXPAND(X) WEIGHT((X), EVAS_HINT_EXPAND, EVAS_HINT_EXPAND)
#define FILL(X) ALIGN((X), EVAS_HINT_FILL, EVAS_HINT_FILL)

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
static Eina_Hash *chats = NULL;

static void
chat_win_key(S2_Auth *sa, Evas *e EINA_UNUSED, Evas_Object *obj, Evas_Event_Key_Down *ev)
{
   if ((!strcmp(ev->keyname, "Return")) || (!strcmp(ev->keyname, "KP_Enter")))
     {
       char *s;
       const char *txt;
       S2_Contact *sc;
       Evas_Object *pane, *entry;

       /* FIXME: add popup error or something? */
       if (shotgun_connection_state_get(sa->auth) != SHOTGUN_CONNECTION_STATE_CONNECTED) return;
       sc = ui_contact_find(sa, evas_object_data_get(obj, "focused"));

       pane = evas_object_data_get(obj, sc->jid);
       entry = elm_object_part_content_get(pane, "elm.swallow.right");

       txt = elm_entry_entry_get(entry);
       if ((!txt) || (!txt[0])) return;

       s = elm_entry_markup_to_utf8(txt);

       ui_shotgun_message_send(sa, sc, s, SHOTGUN_MESSAGE_STATUS_ACTIVE, sc->xhtml_im);
       elm_entry_entry_set(entry, "");
       elm_entry_cursor_end_set(entry);

       free(s);
     }
}

static void
chat_insert(Evas_Object *win, Evas_Object *entry, const char *from, const char *msg)
{
   const char *grp[] =
     {
        "markup/header/receive",
        "markup/header/send"
     };
   size_t tlen = 0, len = 0, mlen, nlen;
   char timebuf[256];
   char *buf, *namebuf, *s;
   const char *markup, *name, *ts;
   Eina_Bool me = !from;
   Evas_Object *ly;

   ly = evas_object_data_get(win, "layout");
   s = elm_entry_utf8_to_markup(msg);
   /* ideally this would be an entry theme with tags for sending/receiving/formatting,
    * but fuck writing a whole new entry theme just for that :/
    */
   markup = elm_layout_data_get(ly, grp[me]);
   if ((!markup) || (!markup[0]))
     markup = NULL;
   mlen = markup ? strlen(markup) : 0;
   name = elm_layout_data_get(ly, "markup/header/name");
   if ((!name) || (!name[0]))
     name = NULL;
   nlen = name ? strlen(name) : 0;

   /* construct timestamp if requested by current theme */
   ts = elm_layout_data_get(ly, "markup/header/timestamp");
   if (ts)
     len = strftime(timebuf, sizeof(timebuf), ts,
                    localtime((time_t[]){ ecore_time_unix_get() }));


   /* construct the name header string */
   if (nlen)
     {
        /* nlen implies that the theme has a %s for the name */
        len += nlen + 1;
        namebuf = alloca(len);
        snprintf(namebuf, len, name, from ?: "Me");
     }
   else
     namebuf = NULL;

   if (mlen)
     {
        /* theme has markup for header */
        len += mlen + 1;
        buf = alloca(len);
        if (tlen && nlen)
          snprintf(buf, len, markup, timebuf, namebuf);
        else if (tlen)
          snprintf(buf, len, markup, timebuf);
        else if (nlen)
          snprintf(buf, len, markup, namebuf);
        else
          snprintf(buf, len, markup, from ?: "Me");
     }
   else if (tlen || nlen)
     {
        /* no overall markup, but timestamp or name requested */
        if (tlen && nlen)
          {
             buf = alloca(len);
             snprintf(buf, len, "%s%s", timebuf, namebuf);
          }
        else if (tlen)
          buf = timebuf;
        else
          buf = namebuf;
     }
   else
     /* no header at all, somebody's not gonna know wtf is going on */
     buf = NULL;

   if (buf)
     elm_entry_entry_append(entry, buf);
   elm_entry_entry_append(entry, s);
   elm_entry_entry_append(entry, "<ps>");
   elm_entry_cursor_end_set(entry);
}

static void
chat_listener(S2_Auth *sa, S2_Contact *sc, const char *msg, Shotgun_Message_Status status EINA_UNUSED, Eina_Bool xhtml_im EINA_UNUSED)
{
   Evas_Object *win, *pane, *entry;

   win = eina_hash_find(chats, &sa->auth);
   pane = evas_object_data_get(win, sc->jid);
   entry = elm_object_part_content_get(pane, "elm.swallow.left");
   chat_insert(win, entry, NULL, msg);
}

static void
chat_win_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   S2_Auth *sa = data;

   eina_stringshare_del(evas_object_data_del(obj, "file"));
   ui_shotgun_message_send_listener_del(sa, chat_listener);
   eina_hash_del_by_key(chats, &sa->auth);
}

static Evas_Object *
chat_win_new(S2_Auth *sa)
{
   Evas_Object *win, *box, *ly, *tb;
   const char *override;
   char buf[PATH_MAX];
   Evas *e;
   Evas_Modifier_Mask ctrl, shift, alt;

   win = elm_win_util_standard_add("chat", NULL);
   evas_object_resize(win, 520, 320);
   evas_object_event_callback_add(win, EVAS_CALLBACK_DEL, chat_win_del, sa);
   evas_object_data_set(win, "auth", sa);
   elm_win_autodel_set(win, EINA_TRUE);
   //evas_object_smart_callback_add(win, "focus,in", (Evas_Smart_Cb)chat_win_focus, sa);
   evas_object_event_callback_add(win, EVAS_CALLBACK_KEY_DOWN, (Evas_Object_Event_Cb)chat_win_key, sa);

   e = evas_object_evas_get(win);
   ctrl = evas_key_modifier_mask_get(e, "Control");
   shift = evas_key_modifier_mask_get(e, "Shift");
   alt = evas_key_modifier_mask_get(e, "Alt");
   1 | evas_object_key_grab(win, "w", ctrl, shift | alt, 1); /* worst warn_unused ever. */
   1 | evas_object_key_grab(win, "Tab", ctrl, alt, 1); /* worst warn_unused ever. */
   1 | evas_object_key_grab(win, "Prior", ctrl, alt, 1); /* worst warn_unused ever. */
   1 | evas_object_key_grab(win, "KP_Prior", ctrl, alt, 1); /* worst warn_unused ever. */
   1 | evas_object_key_grab(win, "Tab", ctrl | shift, alt, 1); /* worst warn_unused ever. */
   1 | evas_object_key_grab(win, "Return", 0, ctrl | shift | alt, 1); /* worst warn_unused ever. */
   1 | evas_object_key_grab(win, "KP_Enter", 0, ctrl | shift | alt, 1); /* worst warn_unused ever. */

   box = elm_box_add(win);
   EXPAND(box);
   FILL(box);
   elm_win_resize_object_add(win, box);
   evas_object_show(box);

   ly = elm_layout_add(win);
   EXPAND(ly);
   FILL(ly);
   override = getenv("S2_THEME_OVERRIDE_DIR");
   snprintf(buf, sizeof(buf), "%s/chat_basic.edj", override ?: PACKAGE_DATA_DIR);
   elm_layout_file_set(ly, buf, "chat");
   evas_object_data_set(win, "file", eina_stringshare_add(buf));
   elm_box_pack_end(box, ly);
   evas_object_data_set(win, "layout", ly);
   evas_object_show(ly);

   tb = elm_toolbar_add(win);
   elm_toolbar_horizontal_set(tb, 1);
   elm_toolbar_shrink_mode_set(tb, ELM_TOOLBAR_SHRINK_SCROLL);
   elm_toolbar_select_mode_set(tb, ELM_OBJECT_SELECT_MODE_ALWAYS);
   elm_object_focus_allow_set(tb, 0);
   elm_object_style_set(tb, "item_horizontal");
   elm_layout_content_set(ly, "swallow.bar", tb);
   evas_object_data_set(win, "toolbar", tb);

   evas_object_show(win);

   /* FIXME: this should be in module init */
   ui_shotgun_message_send_listener_add(sa, chat_listener);

   return win;
}

static void
chat_focus(Evas_Object *pane)
{
   Evas_Object *cur, *ly, *win = elm_object_top_widget_get(pane);
   Elm_Object_Item *it;
   Eina_Stringshare *focused;
   S2_Contact *sc;

   sc = evas_object_data_get(pane, "sc");
   focused = evas_object_data_get(win, "focused");
   /* trying to refocus current focused */
   if (focused == sc->jid) return;
   ly = evas_object_data_get(win, "layout");

   cur = elm_layout_content_unset(ly, "swallow.chat");
   if (cur) evas_object_hide(cur);
   elm_layout_content_set(ly, "swallow.chat", pane);
   evas_object_data_set(win, "focused", sc->jid);
   
   it = evas_object_data_get(pane, "toolbar_item");
   if (it) //can be NULL if selected during creation */
     elm_toolbar_item_selected_set(it, 1);
   elm_object_focus_set(elm_object_part_content_get(pane, "elm.swallow.right"), 1);
}

static void
chat_toolbar_select(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   chat_focus(data);
}

static void
chat_del_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *pane, void *event_info EINA_UNUSED)
{
   Evas_Object *win;
   Elm_Object_Item *it, *next;
   S2_Contact *sc = data;
   Eina_Stringshare *focused;

   win = elm_object_top_widget_get(pane);
   focused = evas_object_data_get(win, "focused");
   it = evas_object_data_get(pane, "toolbar_item");
   if (!it) return; //canvas deleted
   if (focused != sc->jid)
     {
        elm_object_item_del(it);
        return;
     }
   next = elm_toolbar_item_prev_get(it);
   if (!next) elm_toolbar_item_next_get(it);
   if (next)
     {
        elm_toolbar_item_selected_set(next, 1);
        elm_object_item_del(it);
     }
   else
     evas_object_del(win);
}

static void
chat_tb_item_del_cb(void *data, Evas_Object *obj EINA_UNUSED, void *ev EINA_UNUSED)
{
   evas_object_data_del(data, "toolbar_item");
}

static Evas_Object *
chat_new(Evas_Object *win, S2_Contact *sc)
{
   Evas_Object *tb, *pane, *o;
   Elm_Object_Item *it;
   const char *icon = sc->info.photo.data ? NULL : "chat_basic/userunknown";

   tb = evas_object_data_get(win, "toolbar");

   pane = elm_panes_add(win);
   evas_object_event_callback_add(pane, EVAS_CALLBACK_DEL, chat_del_cb, sc);
   evas_object_data_set(pane, "sc", sc);
   evas_object_data_set(win, sc->jid, pane);
   elm_panes_horizontal_set(pane, 1);

   o = elm_entry_add(win);
   elm_entry_cnp_mode_set(o, ELM_CNP_MODE_NO_IMAGE);
   elm_scroller_policy_set(o, ELM_SCROLLER_POLICY_OFF, ELM_SCROLLER_POLICY_AUTO);
   elm_entry_editable_set(o, 0);
   elm_entry_single_line_set(o, 0);
   elm_entry_scrollable_set(o, 1);
   elm_object_focus_allow_set(o, 0);
   elm_entry_line_wrap_set(o, ELM_WRAP_MIXED);
   //elm_entry_markup_filter_append(o, (Elm_Entry_Filter_Cb)chat_filter, sc);
   //evas_object_smart_callback_add(o, "anchor,in", (Evas_Smart_Cb)chat_anchor_in, sc);
   //evas_object_smart_callback_add(o, "anchor,out", (Evas_Smart_Cb)chat_anchor_out, sc);
   //evas_object_smart_callback_add(o, "anchor,clicked", (Evas_Smart_Cb)chat_anchor_click, sc);
   EXPAND(o);
   FILL(o);
   elm_object_part_content_set(pane, "elm.swallow.left", o);
   elm_entry_cursor_end_set(o);
   evas_object_show(o);

   o = elm_entry_add(win);
   elm_entry_scrollable_set(o, 1);
   elm_entry_line_wrap_set(o, ELM_WRAP_MIXED);
   elm_scroller_policy_set(o, ELM_SCROLLER_POLICY_AUTO, ELM_SCROLLER_POLICY_AUTO);
   EXPAND(o);
   FILL(o);
   elm_object_part_content_set(pane, "elm.swallow.right", o);
   //evas_object_smart_callback_add(o, "changed,user", (Evas_Smart_Cb)chat_typing, sc);
   evas_object_show(o);

   it = elm_toolbar_item_append(tb, icon, ui_contact_name_get(sc), chat_toolbar_select, pane);
   if (!icon)
     elm_toolbar_item_icon_memfile_set(it, sc->info.photo.data, sc->info.photo.size, NULL, NULL);
   elm_object_item_del_cb_set(it, chat_tb_item_del_cb);
   evas_object_data_set(pane, "toolbar_item", it);

   elm_panes_content_left_size_set(pane, 0.8);
   return pane;
}

static Eina_Bool
chat_request(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   Evas_Object *win, *o;

   win = eina_hash_find(chats, &sc->auth->auth);
   if (!win)
     {
        win = chat_win_new(sc->auth);
        eina_hash_direct_add(chats, &sc->auth->auth, win);
     }
   elm_win_activate(win);
   o = evas_object_data_get(win, sc->jid);
   if (!o)
     o = chat_new(win, sc);
   chat_focus(o);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
chat_message(void *d EINA_UNUSED, int t EINA_UNUSED, Shotgun_Event_Message *ev)
{
   Evas_Object *win, *pane, *entry;
   const char *jid;

   win = eina_hash_find(chats, &ev->account);
   jid = ui_jid_base_get(ev->jid);
   pane = evas_object_data_get(win, jid);
   if (!pane)
     {
        S2_Contact *sc;

        sc = ui_contact_find(shotgun_data_get(ev->account), jid);
        if (!sc)
          {
             fprintf(stderr, "wtf\n");
             return ECORE_CALLBACK_RENEW;
          }
        pane = chat_new(win, sc);
     }
   entry = elm_object_part_content_get(pane, "elm.swallow.left");
   chat_insert(win, entry, NULL, ev->msg);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
chat_contact_del(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   Evas_Object *win, *o;

   win = eina_hash_find(chats, &sc->auth->auth);
   if (!win) return ECORE_CALLBACK_RENEW;
   o = evas_object_data_get(win, sc->jid);
   if (o) evas_object_del(o);
   return ECORE_CALLBACK_RENEW;
}

EAPI S2_Module_Type
ui_module_type(void)
{
   return S2_MODULE_TYPE_CHAT;
}

EAPI Eina_Bool
ui_module_init(void)
{
   int argc;
   char **argv;

   ecore_app_args_get(&argc, &argv);

   if (!elm_init(argc, argv))
     return EINA_FALSE;

   chats = eina_hash_pointer_new((Eina_Free_Cb)evas_object_del);

   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_CHAT, chat_request, NULL);
   E_LIST_HANDLER_APPEND(handlers, SHOTGUN_EVENT_MESSAGE, chat_message, NULL);
   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_DEL, chat_contact_del, NULL);
   return EINA_TRUE;
}

EAPI void
ui_module_shutdown(void)
{
   void *h;
   EINA_LIST_FREE(handlers, h)
     ecore_event_handler_del(h);
   eina_hash_free(chats);
}
