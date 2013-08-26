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
static Eina_Hash *lists = NULL;


static void
list_item_del(S2_Contact *sa, Evas_Object *obj EINA_UNUSED)
{
   sa->list_item = NULL;
}


static char *
list_item_text_get(S2_Contact *sc, Evas_Object *obj EINA_UNUSED, const char *part)
{
   int ret;
   static const char const *st[] =
   {
      [SHOTGUN_USER_STATUS_NONE] = "Offline?", /* unavailable */
      [SHOTGUN_USER_STATUS_NORMAL] = "Normal",
      [SHOTGUN_USER_STATUS_AWAY] = "Away",
      [SHOTGUN_USER_STATUS_CHAT] = "Chat",
      [SHOTGUN_USER_STATUS_DND] = "Busy",
      [SHOTGUN_USER_STATUS_XA] = "Very Away" /* eXtended Away */
   };

   if (!strcmp(part, "elm.text"))
     return strdup(ui_contact_name_get(sc));
   if (!strcmp(part, "elm.text.sub"))
     {
        char *buf;
        const char *status;
        Shotgun_Event_Presence *p = eina_list_data_get(sc->presences);

        if (!p) return strdup(st[SHOTGUN_USER_STATUS_NONE]);
        status = st[p->status];
        if (!p->description)
          return strdup(status);
        ret = asprintf(&buf, "%s: %s", status, p->description);
        if (ret > 0) return buf;
     }

   return NULL;
}

static Evas_Object *
list_item_status_icon_new(S2_Contact *sc, Shotgun_Event_Presence *p, Eina_Stringshare *file, Evas *e)
{
   Evas_Object *ic;
   const char *sig;
   static const char *st[] =
   {
     [SHOTGUN_USER_STATUS_NONE] = "s2.none",
     [SHOTGUN_USER_STATUS_NORMAL] = "s2.normal",
     [SHOTGUN_USER_STATUS_AWAY] = "s2.away",
     [SHOTGUN_USER_STATUS_CHAT] = "s2.chat",
     [SHOTGUN_USER_STATUS_DND] = "s2.dnd",
     [SHOTGUN_USER_STATUS_XA] = "s2.xa"
   };

   ic = edje_object_add(e);
   edje_object_file_set(ic, file, "status_icon");
   switch (sc->ptype)
     {
      case SHOTGUN_PRESENCE_TYPE_SUBSCRIBE:
        sig = "s2.right";
        break;
      case SHOTGUN_PRESENCE_TYPE_UNSUBSCRIBE:
        sig = "s2.rejected";
      default:
        break;
     }

   if (p)
     {
        Edje_Message_Int_Set msg;

        msg.count = 1;
        msg.val[0] = lround(p->idle);
        edje_object_message_send(ic, EDJE_MESSAGE_INT_SET, 0, &msg);
        edje_object_signal_emit(ic, st[p->status], "s2");
     }
   else if (sc->subscription_pending)
     sig = "s2.left";
   else if (!sc->subscription)
     sig = "s2.x";
   if (!sig)
     {
        switch (sc->subscription)
          {
           case SHOTGUN_USER_SUBSCRIPTION_NONE:
           case SHOTGUN_USER_SUBSCRIPTION_REMOVE:
             sig = "s2.x";
             break;
           case SHOTGUN_USER_SUBSCRIPTION_TO:
             sig = "s2.left";
             break;
           case SHOTGUN_USER_SUBSCRIPTION_FROM:
             sig = "s2.right";
             break;
           case SHOTGUN_USER_SUBSCRIPTION_BOTH:
             sig = "s2.both";
           default:
             break;
          }
     }
   edje_object_signal_emit(ic, sig, "s2");
   edje_object_message_signal_process(ic);
   /* FIXME: holy fuck genlist content sizing is retarded if you aren't using elm_icon */
   //edje_object_calc_force(ic);
   //edje_object_size_min_calc(ic, &w, &h);
   //EXPAND(ic);
   //FILL(ic);
   evas_object_size_hint_min_set(ic, 40, 40);
   evas_object_show(ic);
   return ic;
}

static Evas_Object *
list_item_content_get(S2_Contact *sc, Evas_Object *obj, const char *part)
{
   Evas_Object *ic;
   const char *file;

   file = evas_object_data_get(elm_object_top_widget_get(obj), "file");
   if (!strcmp(part, "elm.swallow.end"))
     {
        ic = elm_icon_add(obj);
        evas_object_size_hint_aspect_set(ic, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
        if (sc->info.photo.data && sc->info.photo.size)
          elm_image_memfile_set(ic, sc->info.photo.data, sc->info.photo.size, NULL, NULL);
        else
          elm_image_file_set(ic, file, "list_basic/userunknown");
        evas_object_show(ic);
        return ic;
     }

   return list_item_status_icon_new(sc, eina_list_data_get(sc->presences), file, evas_object_evas_get(obj));
}

static void
list_item_add(S2_Contact *sc, Evas_Object *win)
{
   static Elm_Genlist_Item_Class glit = {
        .item_style = "double_label",
        .func = {
             .text_get = (Elm_Genlist_Item_Text_Get_Cb)list_item_text_get,
             .content_get = (Elm_Genlist_Item_Content_Get_Cb)list_item_content_get,
             .del = (Elm_Genlist_Item_Del_Cb)list_item_del
        },
        .version = ELM_GENLIST_ITEM_CLASS_VERSION
   };

   sc->list_item = elm_genlist_item_append(evas_object_data_get(win, "list"), &glit, sc, NULL, 0, NULL, NULL);
   shotgun_iq_vcard_get(sc->auth->auth, sc->jid);
}

static void
list_del(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   eina_stringshare_del(evas_object_data_del(obj, "file"));
   ecore_main_loop_quit();
}

static void
overlay_delete(void *data, Evas_Object *obj, const char *sig EINA_UNUSED, const char *src EINA_UNUSED)
{
   Eina_List *l;
   Evas_Object *o;

   evas_object_data_del(data, "overlay");
   l = evas_object_data_del(data, "overlay_objs");
   EINA_LIST_FREE(l, o)
     evas_object_del(o);
   evas_object_del(obj);
}

static char *
overlay_item_text_get(Shotgun_Event_Presence *p, Evas_Object *obj EINA_UNUSED, const char *part)
{
   if (!strcmp(part, "elm.text"))
     {
        const char *jid;

        jid = strchr(p->jid, '/');
        return strdup(jid + 1);
     }
   if (!strcmp(part, "elm.text.sub"))
     {
        if (p->description)
          return strdup(p->description);
     }
   return NULL;
}

static Evas_Object *
overlay_item_content_get(Shotgun_Event_Presence *p, Evas_Object *obj, const char *part)
{
   Evas_Object *ic;
   const char *file;
   char *jid, *s;

   file = evas_object_data_get(elm_object_top_widget_get(obj), "file");
   if (!strcmp(part, "elm.swallow.end"))
     {
        return NULL;
     }

   s = strchr(p->jid, '/');
   jid = strndupa(p->jid, s - p->jid);
   return list_item_status_icon_new(ui_contact_find(shotgun_data_get(p->account), jid), p, file, evas_object_evas_get(obj));
}

static void
overlay_selected(void *data, Evas_Object *obj, void *event_info)
{
   Elm_Object_Item *it = event_info;
   S2_Contact *sc = data;

   
}

static Evas_Object *
overlay_create(Elm_Object_Item *it, Evas *e)
{
   Evas_Object *ic, *box, *win, *o, *ii, *mover;
   int y, w, h, oy, oh, above, below, mw, mh;
   Eina_Stringshare *file;
   Eina_List *objs = NULL;
   S2_Contact *sc;
   Eina_List *l;
   Shotgun_Event_Presence *p;
   static Elm_Genlist_Item_Class glit = {
        .item_style = "double_label",
        .func = {
             .text_get = (Elm_Genlist_Item_Text_Get_Cb)overlay_item_text_get,
             .content_get = (Elm_Genlist_Item_Content_Get_Cb)overlay_item_content_get,
        },
        .version = ELM_GENLIST_ITEM_CLASS_VERSION
   };

   sc = elm_object_item_data_get(it);
   ic = elm_object_item_part_content_get(it, "elm.swallow.end");
   /* get box object for icon so we can find position */
   box = evas_object_smart_parent_get(ic);
   evas_object_geometry_get(box, NULL, &oy, NULL, &oh);
   win = elm_object_top_widget_get(elm_object_item_widget_get(it));
   evas_object_geometry_get(win, NULL, NULL, &w, &h);

   /* commence most complicated placement calculations of all time */
   above = oy - 10;
   if (above < 3 * oh + 20) above = 0;
   below = h - (oy + oh + 10);
   if (below < oh) below = 0;
   /* require at least list-item-height to put things above/below item;
    * abort if we don't have this much
    */
   if ((!above) && (!below)) return NULL;
   file = evas_object_data_get(win, "file");
   mover = edje_object_add(e);
   edje_object_file_set(mover, file, "info_mover");
   box = elm_box_add(win);
   evas_object_show(box);
   objs = eina_list_append(objs, box);
   o = elm_icon_add(win);
   EXPAND(o);
   FILL(o);
   objs = eina_list_append(objs, o);
   evas_object_size_hint_aspect_set(o, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
   if (sc->info.photo.data && sc->info.photo.size)
     elm_image_memfile_set(o, sc->info.photo.data, sc->info.photo.size, NULL, NULL);
   else
     elm_image_file_set(o, file, "list_basic/userunknown");
   evas_object_show(o);
   elm_box_pack_end(box, o);
   o = elm_label_add(win);
   WEIGHT(o, EVAS_HINT_EXPAND, 0);
   objs = eina_list_append(objs, o);
   elm_object_text_set(o, sc->jid);
   evas_object_show(o);
   elm_box_pack_end(box, o);

   ii = elm_genlist_add(win);
   EXPAND(ii);
   FILL(ii);
   elm_scroller_bounce_set(ii, EINA_FALSE, EINA_FALSE);
   elm_scroller_policy_set(ii, ELM_SCROLLER_POLICY_OFF, ELM_SCROLLER_POLICY_OFF);
   elm_genlist_homogeneous_set(ii, EINA_TRUE);
   elm_genlist_mode_set(ii, ELM_LIST_COMPRESS);
   EINA_LIST_FOREACH(sc->presences, l, p)
     elm_genlist_item_append(ii, &glit, p, NULL, 0, overlay_selected, sc);

   evas_object_show(ii);
   elm_box_pack_end(box, ii);
  /* FIXME: size/scroller/something? */

  //edje_extern_object_min_size_set(box, mw, mh);

  if (above < 2 * oh + 20)
    /* not gonna be able to fit image... */
    above = 0;
  if (below < oh)
    below = 0;

  if (above)
    {
       Evas_Object *ob;
       int remain = above - (2 * oh) - 20;
       /* figure out where our item is in the box */
       EINA_LIST_FOREACH(elm_box_children_get(box), l, ob)
         {
            if (remain < oh) break;
            remain -= oh;
         }
       if (remain >= oh)
         {
            /* easymode */
            y = 10 + remain;
         }
       else
         {
            /* ffffffffffuuuuuuuuuuuuuuuuu */
            o = evas_object_rectangle_add(e);
            evas_object_color_set(o, 0, 0, 0, 0);
            evas_object_pass_events_set(o, 1);
            edje_extern_object_min_size_set(o, oh, remain);
            elm_box_pack_after(box, o, ob);
            o = evas_object_rectangle_add(e);
            evas_object_color_set(o, 0, 0, 0, 0);
            evas_object_pass_events_set(o, 1);
            edje_extern_object_min_size_set(o, oh, oh + 10);
            elm_box_pack_after(box, o, ob);
            y = 10;
         }
    }
  else
    y = oy + oh + 2;
  edje_object_part_swallow(mover, "swallow.mover", box);
  //if ((!mw) && (!mh))
  {
     fprintf(stderr, EINA_COLOR_YELLOW "min size calc failed for overlay!\n" EINA_COLOR_RESET);
     mw = MIN(3 * oh, w);
     mh = (4 + eina_list_count(sc->presences)) * oh;
  }
  fprintf(stderr, "OVERLAY: 10,%d %dx%d\n", y, mw, mh);
  evas_object_move(mover, 0, y);
  evas_object_resize(mover, w, MIN(h - 10, mh));
  evas_object_show(mover);
  edje_object_signal_emit(mover, "s2.show", "s2");
  edje_object_signal_callback_add(mover, "s2.hide.done", "", overlay_delete, win);
  evas_object_data_set(win, "overlay", mover);
  evas_object_data_set(win, "overlay_objs", objs);
  evas_object_layer_set(mover, 10);
  return mover;
}

static void
overlay_dismiss(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   edje_object_signal_emit(data, "s2.hide", "s2");
}

static void
list_mouse_down_cb(void *data, Evas *e, Evas_Object *obj, Evas_Event_Mouse_Down *ev)
{
   Elm_Object_Item *it;
   Evas_Object *overlay, *rect, *win;
   int w, h;

   win = elm_object_top_widget_get(obj);
   overlay = evas_object_data_get(win, "overlay");
   if (overlay)
     {
        edje_object_signal_emit(overlay, "s2.hide", "s2");
        return;
     }
   if (ev->button != 3) return;
   it = elm_genlist_at_xy_item_get(data, ev->output.x, ev->output.y, NULL);
   if (!it) return;
   overlay = overlay_create(it, e);
   if (!overlay) return;
   evas_object_geometry_get(win, NULL, NULL, &w, &h);

   rect = evas_object_rectangle_add(e);
   evas_object_resize(rect, w, h);
   evas_object_color_set(rect, 0, 0, 0, 0);
   evas_object_layer_set(rect, 9);
   evas_object_event_callback_add(rect, EVAS_CALLBACK_MOUSE_DOWN, overlay_dismiss, overlay);
}

static Evas_Object *
list_create(S2_Auth *sa)
{
   Evas_Object *win, *box, *ly, *o;
   char buf[PATH_MAX], *override;

   win = elm_win_util_standard_add("S2", "Contacts");
   evas_object_resize(win, 300, 600);
   evas_object_event_callback_add(win, EVAS_CALLBACK_DEL, list_del, NULL);
   evas_object_data_set(win, "auth", sa);
   elm_win_autodel_set(win, EINA_TRUE);

   box = elm_box_add(win);
   EXPAND(box);
   FILL(box);
   elm_win_resize_object_add(win, box);
   evas_object_show(box);

   ly = elm_layout_add(win);
   EXPAND(ly);
   FILL(ly);
   override = getenv("S2_THEME_OVERRIDE_DIR");
   snprintf(buf, sizeof(buf), "%s/list_basic.edj", override ?: PACKAGE_DATA_DIR);
   elm_layout_file_set(ly, buf, "list");
   evas_object_data_set(win, "file", eina_stringshare_add(buf));
   elm_box_pack_end(box, ly);
   evas_object_show(ly);

   o = elm_genlist_add(win);
   evas_object_data_set(win, "list", o);
   elm_genlist_reorder_mode_set(o, EINA_TRUE);
   elm_scroller_bounce_set(o, EINA_FALSE, EINA_FALSE);
   elm_scroller_policy_set(o, ELM_SCROLLER_POLICY_OFF, ELM_SCROLLER_POLICY_AUTO);
   elm_genlist_homogeneous_set(o, EINA_TRUE);
   elm_genlist_mode_set(o, ELM_LIST_COMPRESS);
   elm_layout_content_set(ly, "swallow.list", o);
   evas_object_event_callback_add(ly, EVAS_CALLBACK_MOUSE_DOWN, (Evas_Object_Event_Cb)list_mouse_down_cb, o);
   //evas_object_smart_callback_add(o, "activated",
                                  //(Evas_Smart_Cb)list_click_cb, NULL);
   //evas_object_smart_callback_add(o, "moved",
                                  //(Evas_Smart_Cb)list_reorder_cb, NULL);

   evas_object_show(win);
   return win;
}

static Eina_Bool
presence_add(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   Evas_Object *win;

   win = eina_hash_find(lists, &sc->auth);
   if (!win)
     {
        win = list_create(sc->auth);
        eina_hash_direct_add(lists, &sc->auth, win);
     }

   if (eina_list_count(sc->presences) == 1)
     list_item_add(sc, win);
   else
     elm_genlist_item_update(sc->list_item);
     //{
        //elm_genlist_item_fields_update(sc->list_item, "elm.text", ELM_GENLIST_ITEM_FIELD_TEXT);
        //elm_genlist_item_fields_update(sc->list_item, "elm.text.sub", ELM_GENLIST_ITEM_FIELD_TEXT);
        //elm_genlist_item_fields_update(sc->list_item, "elm.swallow.icon", ELM_GENLIST_ITEM_FIELD_CONTENT);
     //}

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
presence_del(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   if (!sc->list_item) return ECORE_CALLBACK_RENEW;
   if (sc->presences)
     elm_genlist_item_update(sc->list_item);
   else
     elm_object_item_del(sc->list_item);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
presence_update(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   if (sc->list_item)
     elm_genlist_item_update(sc->list_item);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
contact_del(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   if (sc->list_item)
     elm_object_item_del(sc->list_item);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
contact_update(void *d EINA_UNUSED, int t EINA_UNUSED, S2_Contact *sc)
{
   if (sc->list_item)
     elm_genlist_item_update(sc->list_item);
   return ECORE_CALLBACK_RENEW;
}

EAPI S2_Module_Type
ui_module_type(void)
{
   return S2_MODULE_TYPE_LIST;
}

EAPI Eina_Bool
ui_module_init(void)
{
   int argc;
   char **argv;

   ecore_app_args_get(&argc, &argv);

   if (!elm_init(argc, argv))
     return EINA_FALSE;

   lists = eina_hash_pointer_new((Eina_Free_Cb)evas_object_del);

   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_DEL, contact_del, NULL);
   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_UPDATE, contact_update, NULL);
   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_PRESENCE_ADD, presence_add, NULL);
   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_PRESENCE_DEL, presence_del, NULL);
   E_LIST_HANDLER_APPEND(handlers, S2_EVENT_CONTACT_PRESENCE_UPDATE, presence_update, NULL);
   return EINA_TRUE;
}

EAPI void
ui_module_shutdown(void)
{
   void *h;

   EINA_LIST_FREE(handlers, h)
     ecore_event_handler_del(h);

   eina_hash_free(lists);
}
