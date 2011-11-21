#include "util.h"
#include "NodeIteratorVisitorHelper.h"
#include "AbcPreview.h"

#include <maya/MArgList.h>
#include <maya/MArgParser.h>
#include <maya/MFileIO.h>
#include <maya/MFileObject.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MSelectionList.h>
#include <maya/MStringArray.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>
#include <maya/MTime.h>


namespace
{
    MString usage(
"                                                                           \n\
AbcPreview  [options] File                                                 \n\n\
Options:                                                                    \n\
-rpr/ reparent      DagPath                                                 \n\
                    reparent the whole hierarchy under a node in the        \n\
                    current Maya scene                                      \n\
-ftr/ fitTimeRange                                                          \n\
                    Change Maya time slider to fit the range of input file. \n\
-rcs / recreateAllColorSets                                                 \n\
                    IC3/4fArrayProperties with face varying scope on        \n\
                    IPolyMesh and ISubD are treated as color sets even if   \n\
                    they weren't written out of Maya.                       \n\
-ct / connect       string node1 node2 ...                                  \n\
                    The nodes specified in the argument string are supposed to\
 be the names of top level nodes from the input file.                       \n\
                    If such a node doesn't exist in the provided input file, a\
warning will be given and nothing will be done.                             \n\
                    If Maya DAG node of the same name doesn't exist in the    \
current Maya scene,  a warning will be given and nothing will be done.      \n\
                    If such a node exists both in the input file and in the   \
current Maya scene, data for the whole hierarchy from the nodes down        \n\
                    (inclusive) will be substituted by data from the input file,\
 and connections to the delightAlembicArchive will be made or updated accordingly.    \n\
                    If string \"/\" is used as the root name,  all top level  \
nodes from the input file will be used for updating the current Maya scene. \n\
                    Again if certain node doesn't exist in the current scene, \
a warning will be given and nothing will be done.                           \n\
-crt/ createIfNotFound                                                      \n\
                    Used only when -connect flag is set.                    \n\
-rm / removeIfNoUpdate                                                      \n\
                    Used only when -connect flag is set.                    \n\
-sts/ setToStartFrame                                                       \n\
                    Set the current time to the start of the frame range    \n\
-m  / mode          string (\"open\"|\"import\"|\"replace\")                \n\
                    Set read mode to open/import/replace (default to import)\n\
-h  / help          Print this message                                      \n\
-d  / debug         Turn on debug message printout                        \n\n\
Example:                                                                    \n\
AbcPreview -h;                                                               \n\
AbcPreview -d -m open \"/tmp/test.abc\";                                     \n\
AbcPreview -t 1 24 -ftr -ct \"/\" -crt -rm \"/mcp/test.abc\";                \n\
AbcPreview -ct \"root1 root2 root3 ...\" \"/mcp/test.abc\";                  \n"
);  // usage

};


AbcPreview::AbcPreview()
{
}

AbcPreview::~AbcPreview()
{
}

MSyntax AbcPreview::createSyntax()
{
    MSyntax syntax;

    syntax.addFlag("-d",    "-debug",        MSyntax::kNoArg);
    syntax.addFlag("-ftr",  "-fitTimeRange", MSyntax::kNoArg);
    syntax.addFlag("-h",    "-help",         MSyntax::kNoArg);
    syntax.addFlag("-m",    "-mode",         MSyntax::kString);
    syntax.addFlag("-rcs",  "-recreateAllColorSets", MSyntax::kNoArg);

    syntax.addFlag("-ct",   "-connect",          MSyntax::kString);
    syntax.addFlag("-crt",  "-createIfNotFound", MSyntax::kNoArg);
    syntax.addFlag("-rm",   "-removeIfNoUpdate", MSyntax::kNoArg);

    syntax.addFlag("-rpr",  "-reparent",     MSyntax::kString);
    syntax.addFlag("-sts",  "-setToStartFrame",  MSyntax::kNoArg);

    syntax.addArg(MSyntax::kString);

    syntax.enableQuery(true);
    syntax.enableEdit(false);

    return syntax;
}


void* AbcPreview::creator()
{
    return new AbcPreview();
}


MStatus AbcPreview::doIt(const MArgList & args)
{
    MStatus status;

    MArgParser argData(syntax(), args, &status);

    MString filename("");
    MString connectRootNodes("");

    MObject reparentObj = MObject::kNullObj;

    bool    swap = false;
    bool    createIfNotFound = false;
    bool    removeIfNoUpdate = false;

    bool    debugOn = false;

    if (argData.isFlagSet("help"))
    {
        MGlobal::displayInfo(usage);
        return status;
    }

    if (argData.isFlagSet("debug"))
        debugOn = true;

    if (argData.isFlagSet("reparent"))
    {
        MString parent("");
        MDagPath reparentDagPath;
        status = argData.getFlagArgument("reparent", 0, parent);
        if (status == MS::kSuccess
            && getDagPathByName(parent, reparentDagPath) == MS::kSuccess)
        {
            reparentObj = reparentDagPath.node();
        }
        else
        {
            MString theWarning = parent;
            theWarning += MString(" is not a valid DagPath");
            printWarning(theWarning);
        }
    }

    if (!argData.isFlagSet("connect") && argData.isFlagSet("mode"))
    {
        MString modeStr;
        argData.getFlagArgument("mode", 0, modeStr);
        if (modeStr == "replace")
            deleteCurrentSelection();
        else if (modeStr == "open")
        {
            MFileIO fileIo;
            fileIo.newFile(true);
        }
    }
    else if (argData.isFlagSet("connect"))
    {
        swap = true;
        argData.getFlagArgument("connect", 0, connectRootNodes);

        if (argData.isFlagSet("createIfNotFound"))
        {
            createIfNotFound = true;
        }

        if (argData.isFlagSet("removeIfNoUpdate"))
            removeIfNoUpdate = true;
    }

    // if the flag isn't specified we'll only do stuff marked with the Maya
    // meta data
    bool recreateColorSets = false;
    if (argData.isFlagSet("recreateAllColorSets"))
    {
        recreateColorSets = true;
    }

    status = argData.getCommandArgument(0, filename);
    MString abcNodeName;
    if (status == MS::kSuccess)
    {
        {
            MString fileRule, expandName;
            MString alembicFileRule = "alembicCache";
            MString alembicFilePath = "cache/alembic";

            MString queryFileRuleCmd;
            queryFileRuleCmd.format("workspace -q -fre \"^1s\"",
                alembicFileRule);
            MString queryFolderCmd;
            queryFolderCmd.format("workspace -en `workspace -q -fre \"^1s\"`",
                alembicFileRule);

            // query the file rule for alembic cache
            MGlobal::executeCommand(queryFileRuleCmd, fileRule);
            if (fileRule.length() > 0)
            {
                // we have alembic file rule, query the folder
                MGlobal::executeCommand(queryFolderCmd, expandName);
            }

            // resolve the expanded file rule
            if (expandName.length() == 0)
            {
                expandName = alembicFilePath;
            }

            // get the path to the alembic file rule
            MFileObject directory;
            directory.setRawFullName(expandName);
            MString directoryName = directory.resolvedFullName();

            // resolve the relative path
            MFileObject absoluteFile;
            absoluteFile.setRawFullName(filename);
            if (absoluteFile.resolvedFullName() != 
                absoluteFile.expandedFullName())
            {
                // this is a relative path
                MString absoluteFileName = directoryName + "/" + filename;
                absoluteFile.setRawFullName(absoluteFileName);
                filename = absoluteFile.resolvedFullName();
            }
            else
            {
                filename = absoluteFile.resolvedFullName();
            }
        }

		/*
        MFileObject fileObj;
        status = fileObj.setRawFullName(filename);
        if (status == MS::kSuccess && fileObj.exists())
        {
            ArgData inputData(filename, debugOn, reparentObj,
                swap, connectRootNodes, createIfNotFound, removeIfNoUpdate,
                recreateColorSets);
            abcNodeName = createScene(inputData);

            if (inputData.mSequenceStartTime != inputData.mSequenceEndTime &&
                inputData.mSequenceStartTime != -DBL_MAX &&
                inputData.mSequenceEndTime != DBL_MAX)
            {
                if (argData.isFlagSet("fitTimeRange"))
                {
                    MTime sec(1.0, MTime::kSeconds);
                    setPlayback(
                        inputData.mSequenceStartTime * sec.as(MTime::uiUnit()),
                        inputData.mSequenceEndTime * sec.as(MTime::uiUnit()) );
                }

                if (argData.isFlagSet("setToStartFrame"))
                {
                    MTime sec(1.0, MTime::kSeconds);
                    MGlobal::viewFrame( inputData.mSequenceStartTime *
                        sec.as(MTime::uiUnit()) );
                }
            }
        }
        else
        {
            MString theError("In AbcPreview::doIt(), ");
            theError += filename;
            theError += MString(" doesn't exist");
            printError(theError);
        }
		*/
    }

    MPxCommand::setResult(abcNodeName);

    return status;
}

