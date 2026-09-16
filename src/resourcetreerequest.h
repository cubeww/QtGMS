#ifndef QTGMS_RESOURCETREEREQUEST_H
#define QTGMS_RESOURCETREEREQUEST_H
#include "project.h"
enum class ResourceTreeCommand { CreateGroup, RenameGroup, DeleteGroup, Duplicate, AddExisting, Sort, References, Move };
struct ResourceTreeRequest {
    ResourceTreeCommand command = ResourceTreeCommand::CreateGroup;
    ResourceType type = ResourceType::Sprite;
    QList<int> source;
    QList<int> destination;
    int position = -1;
};
Q_DECLARE_METATYPE(ResourceTreeRequest)
#endif
