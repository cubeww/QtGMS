#ifndef QTGMS_GMLCONSTANT_H
#define QTGMS_GMLCONSTANT_H

#include "gmlparser.h"

struct GmlConstant
{
    bool isInteger = false;
    bool isBoolean = false;
    qint64 integer = 0;
    double real = 0;
};

bool evaluateGmlConstant(const GmlNodePtr &node, const GmlEnvironment &environment, GmlConstant &value);

#endif
