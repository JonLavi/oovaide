/*
 * Debugger.cpp
 *
 *  Created on: Feb 15, 2014
 *  \copyright 2014 DCBlaha.  Distributed under the GPL.
 */

#include "Debugger.h"
#include "Gui.h"
#include "FilePath.h"
#include "Debug.h"
#include <climits>

#define DEBUG_DBG 0
#if(DEBUG_DBG)
static DebugFile sDbgFile("Dbg.txt");
#endif


std::string DebuggerLocation::getAsString() const
    {
    OovString location = getFilename();
    if(mLineNum != -1)
	{
	location += ':';
	location.appendInt(mLineNum);
	}
    return location;
    }



Debugger::Debugger():
    mBkgPipeProc(*this), mDebuggerListener(nullptr), mCommandIndex(0),
    mFrameNumber(0), mCurrentThread(1)
    {
    resetFrameNumber();
    // It seems like "-exec-run" must be used to start the debugger,
    // so this is needed to prevent it from running to the end when
    // single stepping is used to start the program.
    toggleBreakpoint(DebuggerBreakpoint("main"));
    }

bool Debugger::runDebuggerProcess()
    {
    bool started = mBkgPipeProc.isIdle();
    if(started)
	{
	OovProcessChildArgs args;
	args.addArg(mDebuggerFilePath);
	args.addArg(mDebuggeeFilePath);
	args.addArg("--interpreter=mi");
	mBkgPipeProc.startProcess(mDebuggerFilePath, args.getArgv());
#if(DEBUG_DBG)
	sDbgFile.printflush("Starting process\n");
#endif
	}
    return started;
    }

void Debugger::resume()
    {
    resetFrameNumber();
    ensureGdbChildRunning();
    sendMiCommand("-exec-continue");
    }

void Debugger::toggleBreakpoint(const DebuggerBreakpoint &br)
    {
    if(getChildState() == GCS_GdbChildRunning)
	{
	interrupt();
	}
    auto iter = std::find(mBreakpoints.begin(), mBreakpoints.end(), br);
    if(iter == mBreakpoints.end())
	{
	mBreakpoints.push_back(br);
	if(getChildState() == GCS_GdbChildPaused)
	    {
	    sendAddBreakpoint(br);
	    }
	}
    else
	{
	mBreakpoints.erase(iter);
	if(getChildState() == GCS_GdbChildPaused)
	    {
	    if(br.mBreakpointNumber != -1)
		{
		sendDeleteBreakpoint(br);
		}
	    }
	}
    }

void Debugger::sendAddBreakpoint(const DebuggerBreakpoint &br)
    {
    OovString command = "-break-insert -f ";
    command += br.getAsString();
    sendMiCommand(command);
    }

void Debugger::sendDeleteBreakpoint(const DebuggerBreakpoint &br)
    {
    OovString command = "-break-delete ";
    command.appendInt(br.mBreakpointNumber);
    sendMiCommand(command);
    }

void Debugger::stepInto()
    {
    resetFrameNumber();
    ensureGdbChildRunning();
    sendMiCommand("-exec-step");
    }

void Debugger::stepOver()
    {
    resetFrameNumber();
    ensureGdbChildRunning();
    sendMiCommand("-exec-next");
    }

void Debugger::interrupt()
    {
    if(!mBkgPipeProc.isIdle() && getChildState() == GCS_GdbChildRunning)
	{
	sendMiCommand("-exec-interrupt");
	}
    }

void Debugger::stop()
    {
    if(!mBkgPipeProc.isIdle())
	{
	sendMiCommand("-gdb-exit");
	mBkgPipeProc.childProcessClose();
	changeChildState(GCS_GdbChildNotRunning);
	}
    }

void Debugger::ensureGdbChildRunning()
    {
    if(runDebuggerProcess())
	{
	for(auto const &br : mBreakpoints)
	    sendAddBreakpoint(br);
	}
    if(getChildState() == GCS_GdbChildNotRunning)
	{
	if(mWorkingDir.length())
	    {
	    std::string dirCmd = "-environment-cd ";
	    dirCmd += mWorkingDir;
	    sendMiCommand(dirCmd);
	    }
	if(mDebuggeeArgs.length())
	    {
	    std::string argCmd = "-exec-arguments ";
	    argCmd += mDebuggeeArgs;
	    sendMiCommand(argCmd);
	    }
	sendMiCommand("-exec-run");
	}
    }

void Debugger::startGetVariable(OovStringRef const variable)
    {
    OovString cmd = "-data-evaluate-expression ";
    cmd += "--thread ";
    cmd.appendInt(mCurrentThread, 10);
    cmd += " --frame ";
    cmd.appendInt(mFrameNumber, 10);
    cmd += ' ';
    cmd += variable;
    sendMiCommand(cmd);
    }

void Debugger::startGetStack()
    {
    sendMiCommand("-stack-list-frames");
    }

void Debugger::startGetMemory(OovStringRef const addr)
    {
    OovString cmd = "-data-read-memory-bytes ";
    cmd += addr;
    sendMiCommand(cmd);
    }

void Debugger::sendMiCommand(OovStringRef const command)
    {
    OovString cmd;
    cmd.appendInt(++mCommandIndex);
    cmd.append(command);
    sendCommand(cmd);
    }

void Debugger::sendCommand(OovStringRef const command)
    {
    OovString cmd = command;
    size_t pos = cmd.find('\n');
    if(pos != std::string::npos)
	{
	if(pos > 0)
	    {
	    if(cmd[pos-1] != '\r')
		cmd.insert(pos, "\r");
	    }
	}
    else
	cmd += "\r\n";
    if(mDebuggerListener)
	{
	mDebuggerListener->DebugOutput(cmd);
	}
    mBkgPipeProc.childProcessSend(cmd);
#if(DEBUG_DBG)
    sDbgFile.printflush("Sent Command %s\n", cmd.c_str());
#endif
    }

void Debugger::onStdOut(OovStringRef const out, int len)\
    {
    mGdbOutputBuffer.append(out, len);
    while(1)
	{
	size_t pos = mGdbOutputBuffer.find('\n');
	if(pos != std::string::npos)
	    {
	    std::string res(mGdbOutputBuffer, 0, pos+1);
	    handleResult(res);
	    mGdbOutputBuffer.erase(0, pos+1);
	    }
	else
	    break;
	}
    }

DebuggerLocation Debugger::getStoppedLocation()
    {
    DebuggerLocation loc;
    if(getChildState() == GCS_GdbChildPaused)
	{
	LockGuard lock(mStatusLock);
	loc = mStoppedLocation;
	}
    return loc;
    }

Debugger::eChangeStatus Debugger::getChangeStatus()
    {
    LockGuard lock(mStatusLock);
    Debugger::eChangeStatus st = CS_None;
    if(!mChangeStatusQueue.empty())
	{
	st = mChangeStatusQueue.front();
	mChangeStatusQueue.pop();
	}
    return st;
    }
GdbChildStates Debugger::getChildState()
    {
    LockGuard lock(mStatusLock);
    GdbChildStates cs = mGdbChildState;
    return cs;
    }
OovString Debugger::getStack()
    {
    LockGuard lock(mStatusLock);
    OovString str = mStack;
    return str;
    }

// The docs say that -stack-select-frame is deprecated for the --frame option.
// When --frame is used, --thread must also be specified.
// The --frame option does work with many commands such as -data-evaluate-expression
void Debugger::setStackFrame(OovStringRef const frameLine)
    {
    OovString line = frameLine;
    size_t pos = line.find(':');
    if(pos != std::string::npos)
	{
	int frameNumber;
	OovString numStr = line;
	numStr.resize(pos);
	if(numStr.getInt(0, 10000, frameNumber))
	    {
	    mFrameNumber = frameNumber;
	    }
	}
    }

OovString Debugger::getVarValue()
    {
    LockGuard lock(mStatusLock);
    std::string str = mVarValue;
    return str;
    }


void Debugger::onStdErr(OovStringRef const out, int len)
    {
    std::string result(out, len);
    if(mDebuggerListener)
	mDebuggerListener->DebugOutput(result);
    }

// gets value within quotes of
//	tagname="value"
static std::string getTagValue(std::string const &wholeStr, char const * const tag)
    {
    std::string val;
    std::string tagStr = tag;
    tagStr += "=\"";
    size_t pos = wholeStr.find(tagStr);
    if(pos != std::string::npos)
	{
        /// @todo - this has to skip escaped quotes
	pos += tagStr.length();
	size_t endPos = wholeStr.find("\"", pos);
	if(endPos != std::string::npos)
	    val = wholeStr.substr(pos, endPos-pos);
	}
    return val;
    }

static DebuggerLocation getLocationFromResult(const std::string &resultStr)
    {
    DebuggerLocation loc;
    OovString line = getTagValue(resultStr, "line");
    int lineNum = 0;
    line.getInt(0, INT_MAX, lineNum);
// For some reason, "fullname" has doubled slashes on Windows, Sometimes "file"
// contains a full good path, but not all the time.
    FilePath fullFn(FilePathFixFilePath(getTagValue(resultStr, "fullname")),
	    FP_File);
    loc.setFileLine(fullFn, lineNum);
//    loc.setFileLine(getTagValue(resultStr, "file"), lineNum);
    return loc;
    }

// Return is end of result tuple.
// A tuple is defined in the GDB/MI output syntax BNF
// Example:		std::vector<WoolBag> mBags
// 15^done,value="{mBags = {
//    <std::_Vector_base<WoolBag, std::allocator<WoolBag> >> =
//      {
//      _M_impl =
//         {
//         <std::allocator<WoolBag>> =
//            {
//            <__gnu_cxx::new_allocator<WoolBag>> =
//               {<No data fields>},
//               <No data fields>
//            },
//         _M_start = 0x5a15a0,
//         _M_finish = 0x5a15a2,
//         _M_end_of_storage = 0x5a15a4
//         }
//      },
//       <No data fields>}}"
//
// Example:		A class containing mModule and mInterface
// 10^done,value="
//      {
//      mModule = 0x8,
//      mInterface =
//          {
//          getResourceName = 0x7625118e <onexit+97>,
//      	putTogether = 0x76251162 <onexit+53>
//          }
//      }"
static size_t getResultTuple(int pos, const std::string &resultStr, std::string &tupleStr)
    {
    size_t startPos = resultStr.find('{', pos);
    size_t endPos = resultStr.find('}', startPos);
    if(endPos != std::string::npos)
	{
	endPos++;	// Include the close brace.
	tupleStr = resultStr.substr(startPos, endPos-startPos);
	}
    return endPos;
    }

void Debugger::handleBreakpoint(const std::string &resultStr)
    {
    OovString brkNumStr = getTagValue(resultStr, "number");
    int brkNum;
    if(brkNumStr.getInt(0, 55555, brkNum))
	{
	DebuggerLocation loc = getLocationFromResult(resultStr);
	mBreakpoints.setBreakpointNumber(loc, brkNum);
	}
    }

void Debugger::handleValue(const std::string &resultStr)
    {
    cDebugResult debRes;
    debRes.parseResult(resultStr);
    mVarValue = debRes.getAsString();
    updateChangeStatus(Debugger::CS_Value);
    if(mDebuggerListener)
	mDebuggerListener->DebugOutput(mVarValue);
    }

// 99^done,stack=[
//    frame={level="0",addr="0x00408d0b",func="printf",file="c:/mingw/include/stdio.h",
//	fullname="c:\\mingw\\include\\stdio.h",line="240"},
//    frame={level="1",...
void Debugger::handleStack(const std::string &resultStr)
    {
	{
	LockGuard lock(mStatusLock);
	mStack.clear();
	int frameNum = 0;
        size_t pos=0;
	do
	    {
	    std::string tuple;
	    pos = getResultTuple(pos, resultStr, tuple);
	    if(pos != std::string::npos)
		{
		mStack.appendInt(frameNum++, 10);
		mStack += ':';
		mStack += getTagValue(tuple, "func");
		mStack += "   ";
		DebuggerLocation loc = getLocationFromResult(tuple);
		mStack += loc.getAsString();
		mStack += "\n";
		}
	    else
		break;
	    } while(pos!=std::string::npos);
	}
    updateChangeStatus(Debugger::CS_Stack);
    }

static int compareSubstr(const std::string &resultStr, size_t pos, char const *substr)
    {
    return resultStr.compare(pos, strlen(substr), substr);
    }

void Debugger::handleResult(const std::string &resultStr)
    {
    // Values are: "^running", "^error", "*stop"
    // "^connected", "^exit"
#if(DEBUG_DBG)
    sDbgFile.printflush("%s\n", resultStr.c_str());
#endif
    if(isdigit(resultStr[0]))
	{
	size_t pos = 0;
	while(isdigit(resultStr[pos]))
	    {
	    pos++;
	    }
	switch(resultStr[pos+0])
	    {
	    case '^':
		{
		// After ^ is the "result-class":
		//	running, done, connected, error, exit
		if(compareSubstr(resultStr, pos+1, "running") == 0)
		    {
		    changeChildState(GCS_GdbChildRunning);
		    }
		else if(compareSubstr(resultStr, pos+1, "error") == 0)
		    {
//		if(mDebuggerListener)
//		    mDebuggerListener->DebugOutput(&resultStr[3]);
		    }
		else if(compareSubstr(resultStr, pos+1, "done") == 0)
		    {
		    size_t variableNamePos = resultStr.find(',');
		    if(variableNamePos != std::string::npos)
			{
			variableNamePos++;
			if(compareSubstr(resultStr, variableNamePos, "type=") == 0)
			    {
			    std::string typeStr = getTagValue(resultStr, "type=");
			    if(typeStr.compare("breakpoint") == 0)
				{
				handleBreakpoint(resultStr);
				}
			    }
			else if(compareSubstr(resultStr, variableNamePos, "stack=") == 0)
			    {
			    handleStack(resultStr);
			    }
			else if(compareSubstr(resultStr, variableNamePos, "value=") == 0)
			    {
			    handleValue(resultStr.substr(variableNamePos));
			    }
			}
		    }
		else if(compareSubstr(resultStr, pos+1, "exit") == 0)
		    {
		    changeChildState(GCS_GdbChildNotRunning);
		    }
		}
		break;
	    }
	}
    else
	{
	switch(resultStr[0])
	    {
	    case '*':
		{
		if(resultStr.compare(1, 7, "stopped") == 0)
		    {
		    std::string reason = getTagValue(resultStr, "reason");
		    if((reason.find("end-stepping-range") != std::string::npos) ||
			    (reason.find("breakpoint-hit") != std::string::npos))
			{
			    {
			    LockGuard lock(mStatusLock);
			    mStoppedLocation = getLocationFromResult(resultStr);
			    }
			changeChildState(GCS_GdbChildPaused);
			}
		    else if(reason.find("exited-normally") != std::string::npos)
			{
			changeChildState(GCS_GdbChildNotRunning);
			}
		    }
		else if(resultStr.compare(1, std::string::npos, "stop") == 0)
		    {
		    changeChildState(GCS_GdbChildNotRunning);
		    }
		}
		break;

	    case '~':
	    case '@':
	    case '&':
    //	    if(mDebuggerListener)
    //		mDebuggerListener->DebugOutput(&resultStr[3]);
		break;
	    }
	}
    if(mDebuggerListener)
	mDebuggerListener->DebugOutput(resultStr);
    }

void Debugger::updateChangeStatus(Debugger::eChangeStatus status)
    {
	{
	LockGuard lock(mStatusLock);
	mChangeStatusQueue.push(status);
	}
    if(mDebuggerListener)
	mDebuggerListener->DebugStatusChanged();
    }

void Debugger::changeChildState(GdbChildStates state)
    {
	{
	LockGuard lock(mStatusLock);
	mGdbChildState = state;
	}
    updateChangeStatus(CS_RunState);
    }
