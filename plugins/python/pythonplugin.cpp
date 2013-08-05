/*****************************************************************************
 * vlyc2 - A Desktop YouTube client
 * Copyright (C) 2013 Orochimarufan <orochimarufan.x3@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <PythonQt.h>
#include <PythonQt_QtAll.h>

#include "pythonplugin.h"
#include "pythonsites.h"
#include "pythonqtdecorator.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QStringList>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>

static PyModuleDef VlycModuleDef = {
    PyModuleDef_HEAD_INIT,
    "vlyc",
    NULL,
    -1,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

PythonPlugin::PythonPlugin(QObject *parent) :
    QObject(parent)
{
    PythonQt::init(PythonQt::RedirectStdOut);
    PythonQt_QtAll::init();
    PythonQt *py = PythonQt::self();

    PyObject *module = PyModule_Create(&VlycModuleDef);
    PyObject *modules = PySys_GetObject("modules");
    PyDict_SetItemString(modules, VlycModuleDef.m_name, module);

    py->registerClass(&PythonPluginModule::staticMetaObject);

    py->addDecorators(new PythonQtDecorator());

    connect(py, SIGNAL(pythonStdOut(QString)), SLOT(write_out(QString)));
    connect(py, SIGNAL(pythonStdErr(QString)), SLOT(write_err(QString)));

    py->addObject(module, "_plugin", &reg);

    QVariantList __path__;
    __path__ << ":/lib/vlyc";
    py->addVariable(module, "__path__", QVariant::fromValue(__path__));

    py->installDefaultImporter();
    //PythonQtObjectPtr importmod = py->importModule("PythonQtImport");
    //py->addVariable(module, "__loader__", importmod.getVariable("PythonQtImporter"));

    // get rid of apport on Ubuntu!
    PyObject *excepthook = PySys_GetObject("__excepthook__");
    PySys_SetObject("excepthook", excepthook);
}

void PythonPlugin::initialize(VlycPluginInitializer init)
{
    initer = init;
    PythonQtObjectPtr module = PythonQt::self()->importModule("vlyc");
    module.addVariable("network", QVariant::fromValue<QObject*>((QObject*)initer.network));
}

void PythonPlugin::write_out(QString s)
{
    puts(qPrintable(s));
}

void PythonPlugin::write_err(QString s)
{
    fputs(qPrintable(s), stderr);
}

PythonPlugin::~PythonPlugin()
{
}

bool PythonPlugin::canHandle(QString path)
{
    QFileInfo f(path);
    //qDebug("Python test path: '%s'' | file(%i) readable(%i) dir(%i) hasinit(%i)", qPrintable(path), f.isFile(), f.isReadable(), f.isDir(), QDir(path).exists("__init__.py"));
    if (f.isFile() && f.isReadable())
        return path.endsWith(".py");
    else if (f.isDir())
        return QDir(path).exists("__init__.py");
    return false;
}

// PythonPluginRegistrar
PythonPluginModule::PythonPluginModule()
{
    reg = NULL;
}

struct RegScope
{
    PythonPluginModule *reg;
    RegScope(PythonPluginModule *reg, VlycForeignPluginRegistrar newReg)
    {
        this->reg = reg;
        reg->reg = newReg;
    }
    ~RegScope()
    {
        reg->reg = NULL;
    }
};

bool PythonPlugin::loadPlugin(QString path, VlycForeignPluginRegistrar registrar)
{
    static long pluginCnt = 0;
    static QString modfmt("VLYCPLUGIN%1_%2");
    QFileInfo f(path);
    RegScope r(&reg, registrar);
    if (f.isFile())
    {
        QStringList modParts = path.split("/").last().split(".");
        modParts.removeLast();
        QString mod = modfmt.arg(pluginCnt++).arg(modParts.join("_"));
        PythonQtObjectPtr module = PythonQt::self()->createModuleFromScript(mod);
        PythonQt::self()->evalFile(module.object(), path);
    }
    else
    {
        QString mod = modfmt.arg(pluginCnt++).arg(path.split("/").last().replace('/', '_').replace('.', '_'));
        QDir folder(path);
        PythonQtObjectPtr package = PythonQt::self()->createModuleFromScript(mod);
        QVariantList __path__;
        __path__ << path;
        package.addVariable("__path__", QVariant::fromValue(__path__));
        PythonQt::self()->evalFile(package, folder.absoluteFilePath("__init__.py"));
    }
    if(PythonQt::self()->hadError())
    {
        PyErr_Print();
        PyErr_Clear();
        return false;
    }
    return true;
}

void PythonPluginModule::registerSite(PyObject *plugin)
{
    if(!reg)
    {
        PyErr_SetString(PyExc_RuntimeError, "the PythonPluginRegistrar instance can only be used while initializing the module!");
        return;
    }
    PythonSitePlugin *it = PythonSitePlugin::create(plugin);
    if (!it)
    {
        qDebug("Could not register a site plugin.");
        return;
    }
    qDebug("Registering Python Site %s by %s", qPrintable(it->name()), qPrintable(it->author()));
    reg(it);
}
