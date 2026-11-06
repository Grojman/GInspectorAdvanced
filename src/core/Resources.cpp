#include "Resources.h"

#include <QtGlobal>

static void initImpl()
{
    Q_INIT_RESOURCE(resources);
}

void giaInitResources()
{
    static bool done = false;
    if (!done) {
        done = true;
        initImpl();
    }
}
