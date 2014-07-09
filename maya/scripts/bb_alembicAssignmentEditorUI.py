
import os,sys

try:
    from PySide import QtGui, QtCore, QtUiTools
    #import shiboken
    
except:
    #ShpCore.printException(a)
    from PyQt4 import QtGui,QtCore
    QtCore.Signal = QtCore.pyqtSignal
    #import sip

# remap some often used objects/modules
Signal = QtCore.Signal
Qt = QtCore.Qt 

from bb_python.gui import Style
from bb_python.gui import MainWindow as BBMainWindow
from bb_python.gui import Widget as BBWidget
from bb_python.Color import Color

try:
    _fromUtf8 = QtCore.QString.fromUtf8
except AttributeError:
    _fromUtf8 = lambda s: s

import cask # alembic cask helper module
import json # for deling with the dictionary attributes
import maya.cmds as _mc
import maya.OpenMaya as OpenMaya
import maya.OpenMayaUI as OpenMayaUI

import pymel.core as _pc
in_maya = True

draggable = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable | QtCore.Qt.ItemIsDragEnabled
droppable = QtCore.Qt.ItemIsDropEnabled
editable  = QtCore.Qt.ItemIsEditable
enabled   = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsSelectable 
checkable   = QtCore.Qt.ItemIsEnabled | QtCore.Qt.ItemIsUserCheckable

from arnold import *

def clearWidget(widget):    
    """Utility method for clearing a widget and any widgets it contains"""
    while widget.layout().count():        
        item = widget.layout().takeAt(0)
        if isinstance(item, QtGui.QWidgetItem):
            item.widget().close()
        elif isinstance(item, QtGui.QSpacerItem):        
            widget.layout().removeItem(item)   
    
def getArnoldShaders(assFile):

   shaders = []
   displacements = []
   AiBegin()

   # load the ass file
   AiASSLoad(assFile,AI_NODE_SHADER)

   # now find all the shaders and displacments

   shaderIter = AiUniverseGetNodeIterator(AI_NODE_SHADER)

   while not AiNodeIteratorFinished(shaderIter):
      this_node = AiNodeIteratorGetNext(shaderIter)
      # get the type of shader
      node_name = AiNodeGetName(this_node)
      node_entry = AiNodeGetNodeEntry(this_node)
      node_type = AiNodeEntryGetName(node_entry)
      print node_name,node_type

      if node_type in ['displacement']:
         displacements.append( (node_name,node_type) )
      else:
         shaders.append( (node_name,node_type) )

   AiNodeIteratorDestroy(shaderIter)

   AiEnd()

   return shaders,displacements

def getArnoldNodeInfo(nodename):

   def GetParamValueAsString(pentry, val, type):
      if type == AI_TYPE_BYTE:
         return str(val.contents.BYTE)   
      elif type == AI_TYPE_INT:
         return str(val.contents.INT)   
      elif type == AI_TYPE_UINT:
         return str(val.contents.UINT)   
      elif type == AI_TYPE_BOOLEAN:
         return "true" if (val.contents.BOOL != 0) else "false"   
      elif type == AI_TYPE_FLOAT:
         return "%g" % val.contents.FLT
      elif type == AI_TYPE_VECTOR or type == AI_TYPE_POINT:
         return "%g, %g, %g" % (val.contents.PNT.x, val.contents.PNT.y, val.contents.PNT.z)
      elif type == AI_TYPE_POINT2:
         return "%g, %g" % (val.contents.PNT.x, val.contents.PNT.y)
      elif type == AI_TYPE_RGB:
         return "%g, %g, %g" % (val.contents.RGB.r, val.contents.RGB.g, val.contents.RGB.b)
      elif type == AI_TYPE_RGBA:
         return "%g, %g, %g, %g" % (val.contents.RGBA.r, val.contents.RGBA.g, val.contents.RGBA.b, val.contents.RGBA.a)
      elif type == AI_TYPE_STRING:
         return val.contents.STR
      elif type == AI_TYPE_POINTER:
         return "%p" % val.contents.PTR
      elif type == AI_TYPE_NODE:
         name = AiNodeGetName(val.contents.PTR)
         return str(name)
      elif type == AI_TYPE_ENUM:
         enum = AiParamGetEnum(pentry)
         return AiEnumGetString(enum, val.contents.INT)
      elif type == AI_TYPE_MATRIX:
         return ""
      elif type == AI_TYPE_ARRAY:
         array = val.contents.ARRAY.contents
         nelems = array.nelements
         if nelems == 0:
            return "(empty)"
         elif nelems == 1:
            if array.type == AI_TYPE_FLOAT:
               return "%g" % AiArrayGetFlt(array, 0)
            elif array.type == AI_TYPE_VECTOR:
               vec = AiArrayGetVec(array, 0)
               return "%g, %g, %g" % (vec.x, vec.y, vec.z)
            elif array.type == AI_TYPE_POINT:
               pnt = AiArrayGetPnt(array, 0)
               return "%g, %g, %g" % (pnt.x, pnt.y, pnt.z)
            elif array.type == AI_TYPE_RGB:
               rgb = AiArrayGetRGB(array, 0)
               return "%g, %g, %g" % (rgb.r, rgb.g, rgb.b)
            elif array.type == AI_TYPE_RGBA:
               rgba = AiArrayGetRGBA(array, 0)
               return "%g, %g, %g" % (rgba.r, rgba.g, rgba.b, rgba.a)
            elif array.type == AI_TYPE_POINTER:
               ptr = cast(AiArrayGetPtr(array, 0), POINTER(AtNode))
               return "%p" % ptr
            elif array.type == AI_TYPE_NODE:
               ptr = cast(AiArrayGetPtr(array, 0), POINTER(AtNode))
               name = AiNodeGetName(ptr)
               return str(name)
            else:
               return ""
         else:
            return "(%u elements)" % nelems  
      else:
         return ""

   nodeInfoDict = {}

   AiBegin()

   node_entry = AiNodeEntryLookUp(nodename)

   if not node_entry:
      AiEnd()
      raise 'Nothing to know about node "%s"' % node

   num_params = AiNodeEntryGetNumParams(node_entry)

   params = []
   for p in range(num_params):
      pentry = AiNodeEntryGetParameter(node_entry, p)
      params.append([AiParamGetName(pentry), p])

   # if sort == 1:
   #    params.sort(lambda x, y: -1 if x[0] < y[0] else 1)

   for pp in params:
      pentry = AiNodeEntryGetParameter(node_entry, pp[1])
      param_type  = AiParamGetType(pentry)
      param_value = AiParamGetDefault(pentry)
      param_name  = AiParamGetName(pentry)

      if param_type == AI_TYPE_ARRAY:
         # We want to know the type of the elements in the array
         array = param_value.contents.ARRAY.contents
         type_string = "%s[]" % AiParamGetTypeName(array.type)
      else:
         type_string = AiParamGetTypeName(param_type)

      # Print it like: <type> <name> <default>
      default = GetParamValueAsString(pentry, param_value, param_type)

      nodeInfoDict[param_name] = {'type':type_string,'default':default}

   AiEnd()

   return nodeInfoDict

def getMayaWindow():
   """
   Get the main Maya window as a QtGui.QMainWindow instance
   @return: QtGui.QMainWindow instance of the top level Maya windows
   """
   ptr = OpenMaya.MQtUtil.mainWindow()
   if ptr is not None:
     return ShpQt.wrapinstance(long(ptr), ShpQt.QtGui.QWidget)

def toQtObject(mayaName):
   '''
   Given the name of a Maya UI element of any type, return the corresponding QWidget or QAction. 
   If the object does not exist, returns None
   '''
   import shiboken
   import maya.OpenMayaUI as apiUI

   ptr = apiUI.MQtUtil.findControl(mayaName)
   if ptr is None:
     ptr = apiUI.MQtUtil.findLayout(mayaName)
     if ptr is None:
         ptr = apiUI.MQtUtil.findMenuItem(mayaName)
         
   if ptr is not None:
     obj=shiboken.wrapInstance(long(ptr), ShpQt.QtCore.QObject)
     return obj

# Widgets

class FloatLabelWidget(QtGui.QFrame):
   """docstring for FloatLabelWidget"""
   def __init__(self, title='test',parent=None):
      super(FloatLabelWidget, self).__init__()
      self.setLayout(QtGui.QVBoxLayout())
      self.label = QtGui.QLabel(title)
      self.layout().addWidget(self.label)
      self.valueWidget = QtGui.QDoubleSpinBox()
      self.layout().addWidget(self.valueWidget)

   def getValue(self):
      return self.valueWidget.value()

class IntLabelWidget(QtGui.QFrame):
   """docstring for IntLabelWidget"""
   def __init__(self, title='test',parent=None):
      super(IntLabelWidget, self).__init__()
      self.setLayout(QtGui.QVBoxLayout())
      self.label = QtGui.QLabel(title)
      self.layout().addWidget(self.label)
      self.valueWidget = QtGui.QSpinBox()
      self.layout().addWidget(self.valueWidget)

   def getValue(self):
      return self.valueWidget.value()

class EnumLabelWidget(QtGui.QFrame):
   """docstring for IntLabelWidget"""
   def __init__(self, title='test',parent=None,options=[]):
      super(IntLabelWidget, self).__init__()
      self.setLayout(QtGui.QVBoxLayout())
      self.label = QtGui.QLabel(title)
      self.layout().addWidget(self.label)
      self.valueWidget = QtGui.QComboBox()
      self.layout().addWidget(self.valueWidget)

      self.setOptions(options)

   def setOptions(self,optionsList):
      for i in range(self.valueWidget.count()):
         self.valueWidget.removeItem(i)      
      for opt in optionsList:
         self.valueWidget.addItem(opt)

   def getValue(self):
      return self.valueWidget.currentText()

class VisibilityLabelWidget(QtGui.QFrame):
   """docstring for VisibilityLabelWidget"""
   def __init__(self, title='test',parent=None):
      super(VisibilityLabelWidget, self).__init__()
      

class ClickableLabel(QtGui.QLabel):
    """docstring for ShpClickableLabel"""

    clicked = QtCore.Signal() # emit collapsed True if the frame is collapsed
    def __init__(self,title):
        super(ClickableLabel, self).__init__(title)

    def mousePressEvent(self,event):
        self.clicked.emit()

class AttrCollapseFrame(QtGui.QFrame):

    collaped = QtCore.Signal(bool) # emit collapsed True if the frame is collapsed
    expanded = QtCore.Signal(bool) # emit expanded True if the frame is expanded

    def __init__(self,title="test",parent=None):
        QtGui.QFrame.__init__(self,parent)
        self.setObjectName('BBCollapseFrame')
        
        # set-up internal layout
        
        self.setLayout(QtGui.QVBoxLayout())
        self.layout().setContentsMargins(0,0,0,0)
        
        # title widget holder
        
        self.titleWidget = QtGui.QWidget(self)
        self.titleWidget.setObjectName('BBCollapseTitleWidget')
        self.titleWidget.setStyleSheet("QWidget#BBCollapseTitleWidget {background:rgba(0,0,0,0)}")
        self.titleWidget.setLayout(QtGui.QHBoxLayout())
        self.titleWidget.layout().setContentsMargins(0,0,0,0)        
        
        # append a collapse icon and label
        
        self.collapseButton = BBWidget.BBPushButton('+')
        self.collapseButton.setFlat(True)
        self.collapseButton.setObjectName("BBCollapseButton")
        self.collapseButton.setStyleSheet("QPushButton#BBCollapseButton {border: 1px solid rgb(0,0,0);font-weight: bold;font-size: 12px}")
        BBWidget.setStaticSize(self.collapseButton,15,15)
        
        self.titleLabel = ClickableLabel(title)
        # self.titleLabel.setStyleSheet("QLabel {color:rgb(200,200,200);background:rgba(0,0,0,0)}")
        self.titleLabel.setSizePolicy(QtGui.QSizePolicy(QtGui.QSizePolicy.Maximum, QtGui.QSizePolicy.Maximum))
        
        
        self.titleWidget.layout().addWidget(self.collapseButton)
        self.titleWidget.layout().addWidget(self.titleLabel) 
        self.titleWidget.layout().insertStretch(-1)    
        
        self.layout().addWidget(self.titleWidget)
        
        # make internal widget with horizontal layout that can be hidden
                      
        self.centreWidget = QtGui.QFrame(self)
        self.centreWidget.setObjectName('BBCollapseFrameCentre')
        
        self.centreWidget.setLayout(QtGui.QVBoxLayout())
#        self.centreWidget.layout().setContentsMargins(0,0,0,0)
        self.setStyleSheet("QFrame#BBCollapseFrame {background:rgba(0,0,0,0)}"\
                            "QFrame#BBCollapseFrameCentre {border: 1px solid rgb(125,125,125);}"\
                            "QWidget {color:rgba(200,200,200,255)}")
        
        self.centreWidget.setHidden(True)
        
        self.layout().addWidget(self.centreWidget)

        self.collapseButton.clicked.connect(self.toggle)    
        self.titleLabel.clicked.connect(self.toggle)
    
    def isCollapsed(self):
        
        if self.centreWidget.isHidden():
            return True
        else:
            return False
    
    def toggle(self):
        """Toggle the collapsed state of the widget"""
        if self.isCollapsed():
            # open the widget            
            self.centreWidget.setVisible(True)
            self.collapseButton.setText('-') 
            self.expanded.emit(True)       
        else:
            # close the widget
            self.centreWidget.setHidden(True)
            self.collapseButton.setText('+')  
            self.collaped.emit(True)           

    def setCollapsed(self):
        if not self.isCollapsed():
            self.centreWidget.setHidden(True)
            self.collapseButton.setText('+')  
            self.collaped.emit(True) 

    def setExpanded(self):
        if self.isCollapsed():
            self.centreWidget.setVisible(True)
            self.collapseButton.setText('-') 
            self.expanded.emit(True)                   

            
    def setTitle(self,title):
        """set the title of the collapse box"""
        self.titleLabel.setText(title)

    def addWidget(self,widget):
        """ Add wiodget to the central widget"""
        self.centreWidget.layout().addWidget(widget)
    
    def title(self):
        return str(self.titleLabel.text())

class AlembicEditorWindow(BBMainWindow.BlueBoltWindow):
   """Base class for alemic editor window"""

   nodes = ['ieProceduralHolder','bb_alembicArchiveNode','gpuCache']   

   # Signals
   selectedNode=QtCore.Signal(list,list,list)

   def __init__(self,parent=None,style=None,title='Alembic Editor'):
      super(AlembicEditorWindow, self).__init__(parent=parent,title=title,style=style)
   
      self.currentSelection = []

      self.resize(800,600) # default window size

      self.centralwidget = QtGui.QWidget(self)
      self.setCentralWidget(self.centralwidget)
      self.mainLayout = QtGui.QHBoxLayout(self.centralwidget)   

      self.setupUI()

      self.coreIds=OpenMaya.MCallbackIdArray()
      CBid = OpenMaya.MEventMessage.addEventCallback( 'SelectionChanged',self.selectionChanged)
      self.coreIds.append(CBid)     

      self.setupActions()
      self.setupConnections()
      self.refreshUI(self.getSelected(),[],[])

   def setupUI(self):

      # treeview
      # ---------------------------------------
      # |object tree | shaders | displacments |
      # ---------------------------------------
      self.treeWidget = QtGui.QTreeWidget(self.centralwidget)
      self.treeWidget.headerItem().setText(0, QtGui.QApplication.translate("MainWindow", "Object", None, QtGui.QApplication.UnicodeUTF8))
      self.treeWidget.headerItem().setText(1, QtGui.QApplication.translate("MainWindow", "Shaders", None, QtGui.QApplication.UnicodeUTF8))
      self.treeWidget.headerItem().setText(2, QtGui.QApplication.translate("MainWindow", "Displacements", None, QtGui.QApplication.UnicodeUTF8))
      self.treeWidget.headerItem().setText(3, QtGui.QApplication.translate("MainWindow", "User Attributes", None, QtGui.QApplication.UnicodeUTF8))

      self.mainLayout.addWidget(self.treeWidget)

      # overrides panel
      # TODO style for dock widgets
      self.overridesPanel = QtGui.QDockWidget(self.centralwidget)
      op_stylesheet = self.overridesPanel.styleSheet()
      self.overridesPanel.setStyleSheet(op_stylesheet+"  QDockWidget { \
                                                           border:1px solid darkgray;\
                                                           border-radius:2px;\
                                                         }\
                                                         QDockWidget::title {\
                                                           text-align: center; /* align the text to the left */\
                                                           padding-left: 5px;\
                                                           border:1px solid rgb(80,80,80);\
                                                           border-radius:2px;\
                                                         }")

      self.overridesPanel.setMinimumSize(QtCore.QSize(250, 41))
      self.overridesPanel.setWindowTitle(QtGui.QApplication.translate("MainWindow", "Overrides", None, QtGui.QApplication.UnicodeUTF8))
      self.overridesPanel.setFeatures(QtGui.QDockWidget.DockWidgetFloatable|QtGui.QDockWidget.DockWidgetMovable)
      self.overridesPanelContents = QtGui.QWidget()
      self.overridesPanelContents.setLayout(QtGui.QVBoxLayout())
      self.overridesPanelContents.setObjectName(_fromUtf8("overridesPanelContents"))
      self.overridesPanel.setWidget(self.overridesPanelContents)
      self.addDockWidget(QtCore.Qt.DockWidgetArea(Qt.RightDockWidgetArea), self.overridesPanel)

   def setupActions(self):

      self.addExpressionAction = None
      self.removeExpressionAction = None
      self.checkExpressionAction = None
      self.applyShaderAction = None
      self.applyDisplacementAction = None

   def setupConnections(self):

      # tree selection -> overrides panel

      self.treeWidget.currentItemChanged.connect(self.populateOverridesPanel)
      self.treeWidget.currentItemChanged.connect(self.setConextMenuActions)
      self.treeWidget.itemChanged.connect(self.setTreeItemValues)

      self.selectedNode.connect(self.refreshUI)

   def refreshUI(self,selection,added,removed):

      self.treeWidget.clear()

      if len(selection):
         usableNodes = []
         for n in _mc.ls(selection,dag=True,ap=True,shapes=True):
            if _pc.PyNode(n).nodeType() in self.nodes:
               usableNodes.append(n)
         self.currentSelection = usableNodes
         self.populateGraph()

   def populateGraph(self):
      for node in self.currentSelection:
         if _pc.PyNode(node).nodeType() in self.nodes:
            self.addNodeToGraph( _pc.PyNode(node) )

   def addNodeToGraph(self,node):

      abcfile,startpoint = self.getAbcFilePath(node) # get the filepath and the object path from the node

      if not cask.is_valid(abcfile):
            print "Not a valid Alembic file", abcfile
            return
      else:
         a = cask.Archive(abcfile).top
         # get to the start point
         if startpoint not in ['/','']:
            a = self.getObject(a,startpoint)

         abc_item = self.walktree(a) # walk the graph         
         AbcIcon = QtGui.QIcon()
         AbcIcon.addPixmap(QtGui.QPixmap( os.path.join(os.path.dirname(__file__),"../icons/abcLogo.png")) )
         abc_item.setIcon(0,AbcIcon)

         top_item = QtGui.QTreeWidgetItem()
         top_item.setText(0,node.nodeName())
         top_item.fullPath = ''
         top_item.addChild(abc_item)
         top_item.setExpanded(True)

         # add the expression tree items

         expressionRootItem =  QtGui.QTreeWidgetItem()
         ExpressionIcon = QtGui.QIcon()
         ExpressionIcon.addPixmap(self.treeWidget.style().standardPixmap(QtGui.QStyle.SP_FileDialogDetailedView))
         expressionRootItem.setIcon(0,ExpressionIcon)

         expressionRootItem.setText(0,"Expressions")
         expressionRootItem.fullPath = 'x'
         expressionRootItem.type = 'expression_root'     

         # get the expressions on the node currently

         self.updateExpressionTree(node,expressionRootItem)

         top_item.addChild(expressionRootItem)

         # self.treeWidget.addTopLevelItem(self.expressionRootItem)

         # add context menus
         self.treeWidget.addTopLevelItem(top_item)
         self.treeWidget.setContextMenuPolicy(Qt.ActionsContextMenu)
         # self.treeWidget.customContextMenuRequested.connect(self.contextMenuEvent)

   def updateExpressionTree(self,node,expressionRoot):
      # get the json attributes
      o_dict,s_dict,d_dict,ua_dict = self.getJSONData(node)


   def getJSONData(self,node):
      overrides = {}
      userAttributes = {}
      shaderAssignation = {}
      displacementAssignation = {}

      if node.hasAttr('overrides') and node.overrides.get() != '':
         try:
            overrides = json.loads(node.overrides.get())
         except ValueError as E:
            pass
      if node.hasAttr('shaderAssignation') and node.shaderAssignation.get() != '':
         try:
            shaderAssignation = json.loads(node.shaderAssignation.get())
         except ValueError as E:
            pass
      if node.hasAttr('displacementAssignation') and node.displacementAssignation.get() != '':
         try:
            displacementAssignation = json.loads(node.displacementAssignation.get())
         except ValueError as E:
            pass
      if node.hasAttr('userAttributes') and node.userAttributes.get() != '':
         try:
            userAttributes = json.loads(node.userAttributes.get())
         except ValueError as E:
            pass

      return overrides,shaderAssignation,displacementAssignation,userAttributes

   def getAbcFilePath(self,node):
      abcFilePath = ''
      abcObjectPath = '/'

      # switch for each nodetype gpuceche first
      if node.nodeType() == 'gpuCache':
         abcFilePath = node.cacheFileName.get()
         abcObjectPath = node.cacheGeomPath.get().replace('|','/')

      return abcFilePath,abcObjectPath

   def setConextMenuActions(self,treeItem):
      # clear the current actions
      for action in self.treeWidget.actions():
         self.treeWidget.removeAction(action)

      # get the type of tree item
      if treeItem.type == 'expression_root':
         if not self.addExpressionAction:
            self.addExpressionAction = QtGui.QAction(u'Add Expression',self)
            self.addExpressionAction.triggered.connect(lambda: self.addExpression(treeItem))
         self.treeWidget.addAction(self.addExpressionAction)
         return
      elif treeItem.type == 'expression':
         if not self.removeExpressionAction:
            self.removeExpressionAction = QtGui.QAction(u'Remove Expression',self)
            self.removeExpressionAction.triggered.connect(lambda: self.removeExpression(treeItem))
         self.treeWidget.addAction(self.removeExpressionAction) 
         if not self.checkExpressionAction:
            self.checkExpressionAction = QtGui.QAction(u'Check Expression',self)
            self.checkExpressionAction.triggered.connect(lambda: self.checkExpression(treeItem))
         self.treeWidget.addAction(self.checkExpressionAction) 

      if not self.applyShaderAction:
         self.applyShaderAction = QtGui.QAction(u'Apply Shader',self)
         self.applyShaderAction.triggered.connect(lambda: self.applyShader(treeItem))

      self.treeWidget.addAction(self.applyShaderAction) 

      if not self.applyDisplacementAction:
         self.applyDisplacementAction = QtGui.QAction(u'Apply Displacement',self)
         self.applyDisplacementAction.triggered.connect(lambda: self.applyDisplacement(treeItem))

      self.treeWidget.addAction(self.applyDisplacementAction)

   def applyShader(self,treeItem,*args,**argv):
      print "apply shader"

   def applyDisplacement(self,treeItem,*args,**argv):
      print "apply displacement"

   def addExpression(self,treeItem,*args,**argv):

      new_expression = QtGui.QTreeWidgetItem()
      new_expression.setFlags(editable|enabled)
      new_expression.setText(0,"*")
      new_expression.fullPath = "*"
      new_expression.type = 'expression'     

      # add maya call back connection

      # get this expression item
      treeItem.addChild(new_expression)
      treeItem.setExpanded(True) # force the expression tree to be expanded

   def checkExpression(self,treeItem,*args,**argv):
      print "check expression"

   def setTreeItemValues(self,item,col):
      if item.type == 'expression':
         _t = str(item.text(col))
         item.fullPath = _t
         self.n_pathBox.setText(_t)

   def removeExpression(self,treeItem,*args,**argv):

      # get selected expression

      this_expr = self.treeWidget.currentItem()
      root = this_expr.parent()
      this_expr_idx = root.indexOfChild(this_expr)
      root.takeChild(this_expr_idx)

   def walktree(self,iobject,parentItem=None):
      """
      Walk the tree of nodes in the alembic file
      """

      item =  QtGui.QTreeWidgetItem()
      item.setText(0,str(iobject.name))
      item.fullPath = iobject.path()
      item.type = 'iobject'
      for child in iobject.children.values():         
         this_treeItem = self.walktree(child, item)
         item.addChild(this_treeItem)

      return item

   def getObject(self,obj,targetPath):

      out_obj = None
      for c in obj.children.values():
         this_path = c.path()
         if this_path == targetPath:
            return c
         else:
            out_obj = self.getObject(c,targetPath)
            if out_obj:
               return out_obj
      
      return None

   def populateOverridesPanel(self,treeItem):
      clearWidget(self.overridesPanelContents)      
      self.n_pathBox = QtGui.QLineEdit(treeItem.fullPath)
      self.n_pathBox.setReadOnly(True)
      self.overridesPanelContents.layout().addWidget(self.n_pathBox)
      # get the current overrides for the selected object.

      scroll_box = QtGui.QScrollArea(self.overridesPanelContents)
      scroll_boxPanel = QtGui.QWidget(scroll_box)
      scroll_boxPanelLayout = QtGui.QVBoxLayout()
      scroll_boxPanel.setLayout(scroll_boxPanelLayout)

      scroll_box.setWidget(scroll_boxPanel)
      scroll_box.setWidgetResizable(True)

      addOverridesButton = QtGui.QPushButton("Add Override")
      addOverridesButton.clicked.connect(lambda: self.addOverride(node,treeItem))
      scroll_boxPanelLayout.addWidget(addOverridesButton)

      addUserAttrButton = QtGui.QPushButton("Add User Attribute")
      addUserAttrButton.clicked.connect(lambda: self.addUserAttribute(node,treeItem))      
      scroll_boxPanelLayout.addWidget(addUserAttrButton)

      overrides_box = AttrCollapseFrame("Overrides",self.overridesPanelContents)
      scroll_boxPanelLayout.addWidget(overrides_box)

      userAttributes_box = AttrCollapseFrame("User Attributes",self.overridesPanelContents)
      scroll_boxPanelLayout.addWidget(userAttributes_box)    
      scroll_boxPanelLayout.insertStretch(-1)
      self.overridesPanelContents.layout().addWidget(scroll_box)


   def addOverride(self,node,treeItem):
      pass

   def addUserAttribute(self,node,treeItem):
      pass


   def showOverridesBox(self):
      """Overrides box for chosing overrides to add to an object"""
      # get the attributes for this node type
      polymesh_dict = getArnoldNodeInfo('polymesh')
      curves_dict = getArnoldNodeInfo('curves')

      # attr listing

      polymesh_attrs_box = AttrCollapseFrame("PolyMesh Attributes",self.overridesPanelContents)
      polymesh_attrs_box.setExpanded()
      for p_attr in polymesh_dict:
         if "[]" not in polymesh_dict[p_attr]['type']: # for now don't accept arrays
            a_label = QtGui.QLabel(p_attr+" "+polymesh_dict[p_attr]['type'])
            polymesh_attrs_box.addWidget(a_label)

      scroll_boxPanelLayout.addWidget(polymesh_attrs_box)

      curves_attrs_box = AttrCollapseFrame("Curves Attributes",self.overridesPanelContents)
      curves_attrs_box.setExpanded()
      for p_attr in curves_dict:
         a_label = QtGui.QLabel(p_attr+" "+curves_dict[p_attr]['type'])
         curves_attrs_box.addWidget(a_label)

      scroll_boxPanelLayout.addWidget(curves_attrs_box)

   # Signals

   def getSelected(self):
      nameList = _mc.ls(sl=True)
      return nameList

   def selectionChanged(self,*args):
      #if not self.active:return
      added=[]
      removed=[]
      all=[]
      old=self.currentSelection
      self.currentSelection=self.getSelected()
      for element in old:
         if element not in self.currentSelection: removed.append(element)
         
      for element in self.currentSelection:
         if element not in old: added.append(element)
      self.selectedNode.emit(self.currentSelection,added,removed)        

   def closeEvent(self,e):
      OpenMaya.MNodeMessage.removeCallbacks(self.coreIds)
      e.accept()

if __name__ == '__main__':
   app = QtGui.QApplication([])

   w = AlembicEditorWindow()
     
   w.show()

   app.exec_()