# indents the opcode at 11 spaces from the left
# and the first argument at 22 spaces, if possible:
# output    opcode    arg1, arg2
# joachim heintz 2012
# using code by andrés cabrera

# -*- coding: utf-8 -*-

import PythonQt.QtGui as pqt

# tell here which keywords (at first position) leave a line unchanged
excpList = ('instr', 'endin', 'if', 'elseif', 'endif', 'opcode', 'endop', 'sr', 'ksmps', 'kr', 'nchnls', '0dbfs')

def exceptions(line, excptlis):
    """returns t if line starts with a word from excptlis"""
    firstWord = line.split()[0]
    if firstWord in excptlis:
        return True
    else:
        return False
        
def comment(line):
    """returns t if line is a comment"""
    if line.lstrip()[0] == ';' or line.lstrip()[0:2] == '/*' or line.lstrip()[0:2] == '*/':
        return True
    else:
        return False

def listUdos(orcText):
    """list all udo names in orcText"""
    res = []
    for line in orcText.splitlines():
        if line.strip() and not comment(line):
            #stop if orc header has ended
            if line.split()[0] == "instr":
                break
            #otherwise append all udo names
            elif line.split()[0] == "opcode":
                udonam = line.split()[1].rstrip(',')
                res.append(udonam)
    return res

        
def indent():
    selection = q.getSelectedText()  # Get current selection
    udoList = listUdos(q.getOrc())
    if selection == "": # If no selection
        orcText = q.getOrc() # use complete orc section
    else:
        orcText = selection

    newOrcText = ""
    for line in orcText.splitlines():
        #do nothing if empty line, comment or exception
        if line.strip() == "" or comment(line) or exceptions(line, excpList):
            newline = line
        else:
            words = line.split()
            pos = 0
            opcdpos = 0
            firstarg = 0
            newline = ""
            for word in words:
                #word is the opcode
                if q.opcodeExists(word) or word in udoList:
                    if opcdpos < 11:
                        newline = '%-11s%s ' % (newline, word)
                        opcdpos = 11 #new position of the opcode
                    else:
                        newline = '%s%s ' % (newline, word)
                        opcdpos = pos + 1
                    pos = opcdpos + len(word)
                    firstarg = 1 #next word is the first argument
                #word is the first argument
                elif firstarg == 1:
                    if pos - opcdpos < 11 and pos < 22:
                        newline = '%-22s%s ' % (newline, word)
                    else:
                        newline = '%s%s ' % (newline, word)
                    firstarg = 0 #reset 
                #all other cases
                else:
                    newline = '%s%s ' % (newline, word)
                    pos = pos + len(word) + 1
        newOrcText = "%s\n%s" % (newOrcText, newline)

    #remove starting newline
    newOrcText = newOrcText[1:]

    if (selection == ""):
        q.setOrc(newOrcText)  # Write all the orchestra section
    else:
        q.insertText(newOrcText)  # Peplaces the current selection


#info and render window
w = pqt.QWidget() # Create main widget
w.setGeometry(50,50, 400,400)
l = pqt.QGridLayout(w) # Layout to organize widgets
w.setLayout(l)
w.setWindowTitle("Csound Code Indentation")
renderButton = pqt.QPushButton("Indent!",w)
text = pqt.QTextBrowser(w)
l.addWidget(renderButton, 1, 0)
l.addWidget(text, 2, 0)
renderButton.connect("clicked()", indent) #execute at click

info = """Indents the opcode name at 11 spaces from the left
and the first argument at 22 spaces, if possible:
output1   opcode1   arg1, arg2
output2   opcode2   arg3

Lines starting with instr / endin, opcode / endop, if / elseif / endif will be left untouched. The same for comment lines or the statements in the orchestra header.

Select any part of your orchestra code and click on the button above. If you do not select anything, the whole orchestra code will be cleaned up.

Joachim Heintz 2012, using code by Andres Cabrera"""
text.setText(info)

w.show()



