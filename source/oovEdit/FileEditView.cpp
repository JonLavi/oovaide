/*
 * FileEditView.cpp
 *
 *  Created on: Oct 26, 2013
 *  \copyright 2013 DCBlaha.  Distributed under the GPL.
 */

#include "FileEditView.h"
#include "FilePath.h"
#include "File.h"
#include "OovString.h"
#include <string.h>


#include "IncludeMap.h"
#include "BuildConfigReader.h"
#include "OovProcess.h"
// This currently does not get the package command line arguments,
// but usually these won't be needed for compilation.
static void getCppArgs(OovStringRef const srcName, OovProcessChildArgs &args)
    {
    ProjectReader proj;
    proj.readOovProject(Project::getProjectDirectory(), BuildConfigAnalysis);
    OovStringVec cppArgs = proj.getCompileArgs();
    for(auto const &arg : cppArgs)
	{
	args.addArg(arg);
	}

    BuildConfigReader cfg;
    std::string incDepsPath = cfg.getIncDepsFilePath(BuildConfigAnalysis);
    IncDirDependencyMapReader incDirMap;
    incDirMap.read(incDepsPath);
    OovStringVec incDirs = incDirMap.getNestedIncludeDirsUsedBySourceFile(srcName);
    for(auto const &dir : incDirs)
	{
	std::string arg = "-I";
	arg += dir;
	args.addArg(arg);
	}
    }



void signalBufferInsertText(GtkTextBuffer *textbuffer, GtkTextIter *location,
        gchar *text, gint len, gpointer user_data);
void signalBufferDeleteRange(GtkTextBuffer *textbuffer, GtkTextIter *start,
        GtkTextIter *end, gpointer user_data);

//static void scrollChild(GtkWidget *widget, GdkEvent *event, gpointer user_data);
//gboolean draw(GtkWidget *widget, void *cr, gpointer user_data);



void FileEditView::init(GtkTextView *textView)
    {
    mTextView = textView;
    mTextBuffer = gtk_text_view_get_buffer(mTextView);
    mIndenter.init(mTextBuffer);
    gtk_widget_grab_focus(GTK_WIDGET(mTextView));
    g_signal_connect(G_OBJECT(mTextBuffer), "insert-text",
          G_CALLBACK(signalBufferInsertText), NULL);
    g_signal_connect(G_OBJECT(mTextBuffer), "delete-range",
          G_CALLBACK(signalBufferDeleteRange), NULL);
//	    g_signal_connect(G_OBJECT(mBuilder.getWidget("EditTextScrolledwindow")),
//		    "scroll-child", G_CALLBACK(scrollChild), NULL);
//    g_signal_connect(G_OBJECT(mTextView), "draw", G_CALLBACK(draw), NULL);
    }

bool FileEditView::openTextFile(OovStringRef const fn)
    {
    setFileName(fn);
    File file(fn, "rb");
    if(file.isOpen())
	{
	fseek(file.getFp(), 0, SEEK_END);
	long fileSize = ftell(file.getFp());
	fseek(file.getFp(), 0, SEEK_SET);
	std::vector<char> buf(fileSize);
	// actualCount can be less than fileSize on Windows due to /r/n
	int actualCount = fread(&buf.front(), 1, fileSize, file.getFp());
	if(actualCount > 0)
	    {
	    gtk_text_buffer_set_text(mTextBuffer, &buf.front(), actualCount);
	    gtk_text_buffer_set_modified(mTextBuffer, FALSE);
	    }
	highlightRequest();
	}
    return(file.isOpen());
    }

bool FileEditView::saveTextFile()
    {
    bool success = false;
    if(mFileName.length() == 0)
	{
	success = saveAsTextFileWithDialog();
	}
    else
	success = true;
    if(success)
	success = saveAsTextFile(mFileName);
    return success;
    }

GuiText FileEditView::getBuffer()
    {
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(mTextBuffer, &start, &end);
    return GuiText(gtk_text_buffer_get_text(mTextBuffer, &start, &end, false));
    }

void FileEditView::highlightRequest()
    {
    if(mFileName.length())
	{
//	int numArgs = 0;
//	char const * cppArgv[40];
	OovProcessChildArgs cppArgs;
	getCppArgs(mFileName, cppArgs);

	FilePath path;
	path.getAbsolutePath(mFileName, FP_File);
	mHighlighter.highlightRequest(path, cppArgs.getArgv(), cppArgs.getArgc());
	}
    }

void FileEditView::gotoLine(int lineNum)
    {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(mTextBuffer, &iter, lineNum-1);
    // Moves the insert and selection_bound marks.
    gtk_text_buffer_place_cursor(mTextBuffer, &iter);
    moveToIter(iter);
    }

void FileEditView::moveToIter(GtkTextIter startIter, GtkTextIter *endIter)
    {
    GtkTextMark *mark = gtk_text_buffer_get_insert(mTextBuffer);
    if(mark)
	{
//		The solution involves creating an idle proc (which is a procedure which
//		GTK will call when it has nothing else to do until told otherwise by the
//		return value of this procedure). In that idle proc the source view is
//		scrolled constantly, until it is determined that the source view has in
//		fact scrolled to the location desired. This was accomplished by using a couple of nice functions:
//		    gtk_get_iter_location to get the location of the cursor.
//		    gtk_text_view_get_visible_rect to get the rectangle of the visible part of the document in the source view.
//		    gtk_intersect to check whether the cursor is in the visible rectangle.
//		gtk_text_buffer_get_iter_at_mark(mTextBuffer, &iter, mark);

	GtkTextIter tempEndIter = startIter;
	if(endIter)
	    tempEndIter = *endIter;
	gtk_text_buffer_select_range(mTextBuffer, &startIter, &tempEndIter);
	gtk_text_buffer_move_mark(mTextBuffer, mark, &startIter);
	Gui::scrollToCursor(mTextView);
//	gtk_text_view_scroll_to_mark(mTextView, mark, 0, TRUE, 0, 0.5);
	}
    }

bool FileEditView::saveAsTextFileWithDialog()
    {
    PathChooser ch;
    OovString filename = mFileName;
    bool saved = false;
    std::string prompt = "Save ";
    prompt += mFileName;
    prompt += " As";
    if(ch.ChoosePath(GTK_WINDOW(mTextView), prompt,
	    GTK_FILE_CHOOSER_ACTION_SAVE, filename))
	{
	saved = saveAsTextFile(filename);
	}
    return saved;
    }

bool FileEditView::saveAsTextFile(OovStringRef const fn)
    {
    size_t writeSize = -1;
    OovString tempFn = fn;
    setFileName(fn);
    tempFn += ".tmp";
    File file(tempFn, "wb");
    if(file.isOpen())
	{
	int size = gtk_text_buffer_get_char_count(mTextBuffer);
	GuiText buf = getBuffer();
	writeSize = fwrite(buf.c_str(), 1, size, file.getFp());
	file.close();
	FileDelete(fn);
	FileRename(tempFn, fn);
	gtk_text_buffer_set_modified(mTextBuffer, FALSE);
	}
    return(writeSize > 0);
    }

bool FileEditView::checkExitSave()
    {
    bool exitOk = true;
    if(gtk_text_buffer_get_modified(mTextBuffer))
	{
	std::string prompt = "Save ";
	prompt += mFileName;
	GtkDialog *dlg = GTK_DIALOG(gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", prompt.c_str()));
	gtk_dialog_add_button(dlg, GUI_CANCEL, GTK_RESPONSE_CANCEL);
	gint result = gtk_dialog_run(dlg);
	if(result == GTK_RESPONSE_YES)
	    {
	    if(mFileName.length() > 0)
		{
		exitOk = saveTextFile();
		}
	    else
		{
		exitOk = saveAsTextFileWithDialog();
		}
	    }
	else if(result == GTK_RESPONSE_NO)
	    exitOk = true;
	else
	    exitOk = false;
	gtk_widget_destroy(GTK_WIDGET(dlg));
	}
    return exitOk;
    }

bool FileEditView::find(char const * const findStr, bool forward, bool caseSensitive)
    {
    GtkTextMark *mark = gtk_text_buffer_get_insert(mTextBuffer);
    GtkTextIter startFind;
    gtk_text_buffer_get_iter_at_mark(mTextBuffer, &startFind, mark);
    GtkTextIter startMatch;
    GtkTextIter endMatch;
    GtkTextSearchFlags flags = static_cast<GtkTextSearchFlags>(GTK_TEXT_SEARCH_TEXT_ONLY |
		GTK_TEXT_SEARCH_VISIBLE_ONLY);
    if(!caseSensitive)
	{
	flags = static_cast<GtkTextSearchFlags>(flags | GTK_TEXT_SEARCH_CASE_INSENSITIVE);
	}
    bool found = false;
    if(forward)
	{
	gtk_text_iter_forward_char(&startFind);
	found = gtk_text_iter_forward_search(&startFind, findStr, flags,
		&startMatch, &endMatch, NULL);
	}
    else
	{
	gtk_text_iter_backward_char(&startFind);
	found = gtk_text_iter_backward_search(&startFind, findStr, flags,
		&startMatch, &endMatch, NULL);
	}
    if(found)
	moveToIter(startMatch, &endMatch);
    return found;
    }

std::string FileEditView::getSelectedText()
    {
    GtkTextIter startSel;
    GtkTextIter endSel;
    std::string text;
    bool haveSel = gtk_text_buffer_get_selection_bounds(mTextBuffer, &startSel, &endSel);
    if(haveSel)
	{
	GuiText gText = gtk_text_buffer_get_text(mTextBuffer, &startSel, &endSel, true);
	text = gText;
	}
    return text;
    }

bool FileEditView::findAndReplace(char const * const findStr, bool forward,
	bool caseSensitive, char const * const replaceStr)
    {
    GtkTextIter startSel;
    GtkTextIter endSel;
    bool haveSel = gtk_text_buffer_get_selection_bounds(mTextBuffer, &startSel, &endSel);
    if(haveSel)
	{
	bool match;
	GuiText text = gtk_text_buffer_get_text(mTextBuffer, &startSel, &endSel, true);
	if(caseSensitive)
	    {
	    match = (strcmp(text.c_str(), findStr) == 0);
	    }
	else
	    {
	    match = (StringCompareNoCase(text.c_str(), findStr) == 0);
	    }
	if(match)
	    {
	    gtk_text_buffer_delete_selection(mTextBuffer, true, true);
	    gtk_text_buffer_insert_at_cursor(mTextBuffer, replaceStr,
		    strlen(replaceStr));
	    }
	}
    return find(findStr, forward, caseSensitive);
    }

void FileEditView::bufferInsertText(GtkTextBuffer *textbuffer, GtkTextIter *location,
        gchar *text, gint len)
    {
    if(!doingHistory())
	{
	int offset = HistoryItem::getOffset(location);
	addHistoryItem(HistoryItem(true, offset, text, len));
	}
    highlightRequest();
    }

void FileEditView::bufferDeleteRange(GtkTextBuffer *textbuffer, GtkTextIter *start,
        GtkTextIter *end)
    {
    if(!doingHistory())
	{
	int offset = HistoryItem::getOffset(start);
	GuiText str(gtk_text_buffer_get_text(textbuffer, start, end, false));
	addHistoryItem(HistoryItem(false, offset, str, str.length()));
	}
    highlightRequest();
    }

bool FileEditView::idleHighlight()
    {
    bool foundToken = false;
    eHighlightTask task = mHighlighter.highlightUpdate(mTextView, getBuffer(),
		gtk_text_buffer_get_char_count(mTextBuffer));
    if(task & HT_FindToken)
        {
        foundToken = true;
        }
    if(task & HT_ShowMembers)
        {
        OovStringVec members = mHighlighter.getShowMembers();
	GtkTextView *findView = GTK_TEXT_VIEW(Builder::getBuilder()->
		getWidget("FindTextview"));
	Gui::clear(findView);
	for(auto const &str : members)
	    {
	    std::string appendStr = str;
	    appendStr += '\n';
	    Gui::appendText(findView, appendStr);
	    }
        }
    return foundToken;
    }

bool FileEditView::handleIndentKeys(GdkEvent *event)
    {
    bool handled = false;
    int modKeys = (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK) & event->key.state;
    switch(event->key.keyval)
	{
	case '>':	// Checking for "->"
	case '.':
	    {
	    GtkTextIter cursorIter = getCursorIter();
	    int offset = getCursorOffset();
	    offset--;
	    if(event->key.keyval == '>')
		{
		GtkTextIter prevCharIter = cursorIter;
		gtk_text_iter_backward_char(&prevCharIter);
		GuiText str(gtk_text_buffer_get_text(mTextBuffer,
			&prevCharIter, &cursorIter, false));
		if(str[0] == '-')
		    offset--;
		else
		    offset = -1;
		}
	    if(offset != -1)
		{
		mHighlighter.showMembers(offset);
		}
	    }
	    break;

	case GDK_KEY_ISO_Left_Tab:	// This is shift tab on PC
	    // On Windows, modKeys==0, on Linux, modKeys==GDK_SHIFT_MASK
	    if(event->key.state == GDK_SHIFT_MASK || modKeys == 0 ||
		    modKeys == GDK_SHIFT_MASK)
		{
		if(mIndenter.shiftTabPressed())
		    handled = true;
		}
	    break;

	case GDK_KEY_BackSpace:
	    if(modKeys == 0)
		{
		if(mIndenter.backspacePressed())
		    handled = true;
		}
	    break;

	case GDK_KEY_Tab:
	    if(modKeys == 0)
		{
		if(mIndenter.tabPressed())
		    handled = true;
		}
	    break;

	case GDK_KEY_KP_Home:
	case GDK_KEY_Home:
	    if(modKeys == 0)
		{
		if(mIndenter.homePressed())
		    {
		    Gui::scrollToCursor(getTextView());
		    handled = true;
		    }
		}
	    break;
	}
    return handled;
    }

GtkTextIter FileEditView::getCursorIter() const
    {
    GtkTextMark *mark = gtk_text_buffer_get_insert(mTextBuffer);
    GtkTextIter curLoc;
    gtk_text_buffer_get_iter_at_mark(mTextBuffer, &curLoc, mark);
    return curLoc;
    }

int FileEditView::getCursorOffset() const
    {
    GtkTextIter curLoc = getCursorIter();
    return gtk_text_iter_get_offset(&curLoc);
    }

