#include "config_routes.h"

#include "config_routes_internal.h"

void registerConfigRoutes()
{
  registerSystemConfigRoutes();
  registerTemplateRoutes();
  registerBoardRoutes();
}
