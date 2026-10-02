#include "internal.h"
#include "qa/map_sidecars.h"
#include <string.h>

const qa_map_sidecars *qa_application_map_sidecars(const qa_application *application)
{
    qa_application_map_view map;
    const qa_map_sidecars *owner = application ? application->map_sidecars : NULL;
    const qa_launch_snapshot *launch = application ? qa_application_launch(application) : NULL;
    if (!launch && application && application->operation == APPLICATION_PERSISTING)
        launch = application->routing_snapshot;
    const qa_launch_choices *choices = launch ? qa_launch_snapshot_choices(launch) : NULL;
    return owner && qa_application_map_read(application, &map) &&
        qa_map_sidecars_current(owner) && map.resource == qa_map_sidecars_map(owner) &&
        choices && choices->world.map && !strcmp(choices->world.map, qa_map_sidecars_map_path(owner)) ? owner : NULL;
}
