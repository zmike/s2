#include "S2.h"

EAPI S2_Module_Type
ui_module_type(void)
{
   return S2_MODULE_TYPE_LOGIN;
}

EAPI Eina_Bool
ui_module_init(void)
{
   Shotgun_Auth *auth;
   const char *pass;
   char *getpass_x(const char *prompt);

   auth = shotgun_new(getenv("UI_SERVER"), getenv("UI_NAME"), getenv("UI_DOMAIN"));
   EINA_SAFETY_ON_NULL_RETURN_VAL(auth, EINA_FALSE);
   pass = getenv("UI_PASSWORD");
   if (!pass)
     pass = getpass_x("Password: ");
   if (!pass)
     {
        fprintf(stderr, EINA_COLOR_RED "No password entered!\n" EINA_COLOR_RESET);
        return EINA_FALSE;
     }
   shotgun_password_set(auth, pass);
   return shotgun_connect(auth);
}
