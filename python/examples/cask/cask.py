#-******************************************************************************
#
# Copyright (c) 2012-2013,
#  Sony Pictures Imageworks Inc. and
#  Industrial Light & Magic, a division of Lucasfilm Entertainment Company Ltd.
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
# *       Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# *       Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
# *       Neither the name of Sony Pictures Imageworks, nor
# Industrial Light & Magic, nor the names of their contributors may be used
# to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#-******************************************************************************

import os
import re
import imath
import alembic
from functools import wraps

# maps cask objects to alembic "I" objects
IOBJECTS = {
    "Camera": alembic.AbcGeom.ICamera,
    "Collections": alembic.AbcCollection.ICollections,
    "Curve": alembic.AbcGeom.ICurves,
    "FaceSet": alembic.AbcGeom.IFaceSet,
    "Light": alembic.AbcGeom.ILight,
    "Material": alembic.AbcMaterial.IMaterial,
    "NuPatch": alembic.AbcGeom.INuPatch, 
    "Points": alembic.AbcGeom.IPoints,
    "PolyMesh": alembic.AbcGeom.IPolyMesh,
    "SubD": alembic.AbcGeom.ISubD,
    "Xform": alembic.AbcGeom.IXform,
}

# maps cask objects to alembic "O" objects
OOBJECTS = {
    "Camera": alembic.AbcGeom.OCamera,
    "Collections": alembic.AbcCollection.OCollections,
    "Curve": alembic.AbcGeom.OCurves,
    "FaceSet": alembic.AbcGeom.OFaceSet,
    "Light": alembic.AbcGeom.OLight,
    "Material": alembic.AbcMaterial.OMaterial,
    "NuPatch": alembic.AbcGeom.ONuPatch, 
    "Points": alembic.AbcGeom.OPoints,
    "PolyMesh": alembic.AbcGeom.OPolyMesh,
    "SubD": alembic.AbcGeom.OSubD,
    "Xform": alembic.AbcGeom.OXform,
}

# maps cask objects to alembic "I" schemas
ISCHEMAS = {
    "Camera": alembic.AbcGeom.ICameraSchema,
    "Collections": alembic.AbcCollection.ICollectionsSchema,
    "Curve": alembic.AbcGeom.ICurvesSchema,
    "FaceSet": alembic.AbcGeom.IFaceSetSchema,
    "Light": alembic.AbcGeom.ILightSchema,
    "Material": alembic.AbcMaterial.IMaterialSchema,
    "NuPatch": alembic.AbcGeom.INuPatchSchema, 
    "Points": alembic.AbcGeom.IPointsSchema,
    "PolyMesh": alembic.AbcGeom.IPolyMeshSchema,
    "SubD": alembic.AbcGeom.ISubDSchema,
    "Xform": alembic.AbcGeom.IXformSchema,
}

# "O" property class index
OPROPERTIES = {
    bool: {
        'scalar': alembic.Abc.OBoolProperty,
        'array': alembic.Abc.OBoolArrayProperty,
    },
    float: {
        'scalar': alembic.Abc.OFloatProperty,
        'array': alembic.Abc.OFloatArrayProperty,
    },
    int: {
        'scalar': alembic.Abc.OInt32Property,
        'array': alembic.Abc.OInt32ArrayProperty,
    },
    str: {
        'scalar': alembic.Abc.OStringProperty,
        'array': alembic.Abc.OStringArrayProperty,
    },
    imath.BoolArray: {
        'scalar': alembic.Abc.OBoolArrayProperty,
        'array': alembic.Abc.OBoolArrayProperty,
    },
    imath.M33d: {
        'scalar': alembic.Abc.OM33dProperty,
        'array': alembic.Abc.OM33dArrayProperty,
    },
    imath.M44d: {
        'scalar': alembic.Abc.OM44dProperty,
        'array': alembic.Abc.OM44dArrayProperty,
    },
    imath.V2fArray: {
        'scalar': alembic.Abc.OV2fArrayProperty,
        'array': alembic.Abc.OV2fArrayProperty,
    },
    imath.V3fArray: {
        'scalar': alembic.Abc.OV3fArrayProperty,
        'array': alembic.Abc.OV3fArrayProperty,
    },
    imath.V3dArray: {
        'scalar': alembic.Abc.OV3dArrayProperty,
        'array': alembic.Abc.OV3dArrayProperty,
    },
    imath.UnsignedCharArray: {
        'scalar': alembic.Abc.OUcharArrayProperty,
        'array': alembic.Abc.OUcharArrayProperty,
    },
    imath.UnsignedIntArray: {
        'scalar': alembic.Abc.OUInt32ArrayProperty,
        'array': alembic.Abc.OUInt32ArrayProperty,
    },
    imath.IntArray: {
        'scalar': alembic.Abc.OInt32ArrayProperty,
        'array': alembic.Abc.OInt32ArrayProperty,
    },
    imath.FloatArray: {
        'scalar': alembic.Abc.OFloatArrayProperty,
        'array': alembic.Abc.OFloatArrayProperty,
    },
    imath.StringArray: {
        'scalar': alembic.Abc.OStringArrayProperty,
        'array': alembic.Abc.OStringArrayProperty,
    },
    imath.Box2s: {
        'scalar': alembic.Abc.OBox2sProperty,
        'array': alembic.Abc.OBox2sArrayProperty,
    },
    imath.Box2i: {
        'scalar': alembic.Abc.OBox2iProperty,
        'array': alembic.Abc.OBox2iArrayProperty,
    },
    imath.Box2d: {
        'scalar': alembic.Abc.OBox2dProperty,
        'array': alembic.Abc.OBox2dArrayProperty,
    },
    imath.Box2f: {
        'scalar': alembic.Abc.OBox2fProperty,
        'array': alembic.Abc.OBox2fArrayProperty,
    },
    imath.Box3s: {
        'scalar': alembic.Abc.OBox3sProperty,
        'array': alembic.Abc.OBox3sArrayProperty,
    },
    imath.Box3i: {
        'scalar': alembic.Abc.OBox3iProperty,
        'array': alembic.Abc.OBox3iArrayProperty,
    },
    imath.Box3d: {
        'scalar': alembic.Abc.OBox3dProperty,
        'array': alembic.Abc.OBox3dArrayProperty,
    },
    imath.Box3f: {
        'scalar': alembic.Abc.OBox3fProperty,
        'array': alembic.Abc.OBox3fArrayProperty,
    },
    imath.Color3f: {
        'scalar': alembic.Abc.OC3fProperty,
        'array': alembic.Abc.OC3fArrayProperty,
    },
    imath.C3fArray: {
        'scalar': alembic.Abc.OC3fArrayProperty,
        'array': alembic.Abc.OC3fArrayProperty,
    },
    'color': alembic.Abc.OC3fProperty,
    'colour': alembic.Abc.OC3fProperty,
    'shadow_color': alembic.Abc.OC3fProperty,
    'shadow_colour': alembic.Abc.OC3fProperty,
    ".shaderNames": alembic.Abc.OStringArrayProperty,
    'slideMap': alembic.Abc.OStringProperty,
    ".ops": alembic.Abc.OUcharProperty,
}

_COMPOUND_PROPERTY_VALUE_ERROR_ = "Compound properties cannot have values"

def get_simple_oprop_class(name, value):
    """
    Returns the alembic simple property class based on a given name and value.

    :param name: name of the property
    :param value: property value
    :return: alembic "O" property class
    """
    if type(value) in [list, set]:
        _value = value[0]
    else:
        _value = value
    try:
        return OPROPERTIES.get(str(name).lower(),
               OPROPERTIES.get(type(_value)).get(
                       {
                        True:'array', 
                        False:'scalar'
                       }.get((type(value) in [set, list] and len(value) > 1))
                   )
               )
    except AttributeError:
        print "Error finding property class for", name, "with value", value
        return None

def _delist(val):
    """returns single value if list len is 1"""
    return val[0] if type(val) in [list, set] and len(val) == 1 else val

def memoize(func):
    """
    Simple memoization decorator function.
    """
    cache = {}
    @wraps(func)
    def _wrap(*args):
        """cache args"""
        if args not in cache:
            cache[args] = func(*args)
        return cache[args]
    return _wrap

def wrapped(func):
    """
    This decorator function decorates Object methods that require
    access to the alembic schema class.
    """
    @wraps(func)
    def with_wrapped_object(*args, **kwargs):
        """wraps internal alembic iobject"""
        iobj = args[0].iobject
        for klass in IOBJECTS.values():
            if iobj and klass.matches(iobj.getMetaData()):
                args[0].iobject = klass(iobj.getParent(), iobj.getName())
        return func(*args, **kwargs)
    return with_wrapped_object

def wrap(iobject):
    """
    Returns a cask-wrapped class object based on the class method "matches".
    """
    if iobject.getName() == "ABC":
        return Top(iobject)
    for cls in Object.__subclasses__():
        if cls.matches(iobject):
            return cls(iobject)
    return iobject

def is_valid(archive):
    """
    Returns True if the archive is a valid alembic archive.
    """
    try:
        alembic.Abc.IArchive(archive)
        return True
    except RuntimeError:
        return False

def find(obj, name):
    """
    Finds and returns a list of Objects with names matching
    a given regular expression. ::

        >>> find(a.top, ".*Shape")
        [<PolyMesh "cube1Shape">, <PolyMesh "cube2Shape">]

    :param name: Regular expression to match object name
    :return: Sorted list of Object results
    """
    results = [r for r in find_iter(obj, name)]
    return sorted(results, key=lambda x: x.name)

def find_iter(obj, name):
    """
    Generator that yields Objects with names matching
    a given regular expression.

    :param name: Regular expression to match object name
    :yields: Object with name matching name regex
    """
    if re.match(name, obj.name):
        yield obj
    else:
        for child in obj.children.values():
            for obj in find_iter(child, name):
                yield obj

def _deep_getitem(access_func, key):
    """
    Facilitates deep dict get item on DeepDict class.
    """
    split_key = key.split('/')
    start = split_key[0]
    rest = '/'.join(split_key[1:])
    return access_func(start).get_item(rest)

class DeepDict(dict):
    """
    Special dict subclass that allows deep dictionary access.
    """
    def __init__(self, parent):
        super(DeepDict, self).__init__()
        self.parent = parent

    def __getitem__(self, item):
        if type(item) == str:
            if item.startswith("/"):
                item = item[1:]
            if item.endswith("/"):
                item = item[:-1]
            if "/" in item:
                return _deep_getitem(self.__getitem__, item)
            return super(DeepDict, self).__getitem__(item)
        
    def __setitem__(self, name, item):
        obj = self.parent
        new = False
        if "/" in name:
            names = name.split("/")
            for name in names:
                try:
                    obj = obj.get_item(name)
                except KeyError:
                    if name != names[-1]:
                        raise
                    new = True
            if new is False:
                obj = obj.parent
            return obj.set_item(name, item)
        item._name = name
        item._parent = obj
        return super(DeepDict, self).__setitem__(name, item)

    def remove(self, key):
        """Removes an item if it exists."""
        if key and self.has_key(key):
            self.pop(key)

class Archive(object):
    """Archive I/O Object"""
    def __init__(self, filepath=None, fps=24):
        """
        Creates a new Archive class object.

        :param filepath: Path to Alembic archive file.
        :param fps: Frames per second (default 24).
        """
        if filepath and not os.path.isfile(filepath):
            raise RuntimeError("Nonexistent file: %s" % filepath)

        # set some default values
        self.filepath = None
        self._iobject = None
        self._oobject = None
        self._top = None
        self.fps = fps
        self.time_sampling_id = 0

        # read in the archive
        self.__read_from_file(filepath)

    def __repr__(self):
        return '<%s "%s">' % (self.__class__.__name__, self.filepath)

    def __get_iobject(self):
        """gets iobject"""
        if self._iobject is None:
            if self.filepath and os.path.exists(self.filepath):
                self._iobject = alembic.Abc.IArchive(self.filepath)
        return self._iobject

    def __set_iobject(self, iobject):
        """sets iobject"""
        self._iobject = iobject

    iobject = property(__get_iobject, __set_iobject, 
                       doc="Internal Alembic IArchive object.")

    def __get_oobject(self):
        """gets oobject"""
        if self._oobject is None:
            if self.filepath and not os.path.exists(self.filepath):
                self._oobject = alembic.Abc.OArchive(self.filepath)
                self.top.oobject = self._oobject.getTop()
        return self._oobject

    def __set_oobject(self, oobject):
        """sets oobject"""
        self._oobject = oobject

    oobject = property(__get_oobject, __set_oobject, 
                       doc="Internal Alembic OArchive object.")

    def __get_top(self):
        """gets the top object"""
        if not self._top:
            self._top = Top(self)
            if self.iobject:
                self._top = Top(self, self.iobject.getTop())
            if self.oobject:
                if not self._top:
                    self._top = Top(self, self.oobject.getTop())
                self._top.oobject = self.oobject.getTop()
        return self._top

    def __set_top(self, top):
        """sets the top object"""
        self._top = top

    top = property(__get_top, __set_top, 
                   doc="Hierarchy root, cask.Top object.")

    def __read_from_file(self, filepath):
        """
        Reads and sets the internal IArchive object.
        """
        self.filepath = filepath
        self.iobject = None
        self.oobject = None
        self.top = None
        self.time_sampling_id = 0
        self.__get_iobject()

    def info(self):
        """Returns a metadata dictionary."""
        return alembic.Abc.GetArchiveInfo(self.iobject)

    def alembic_version(self):
        """
        Returns the version of alembic used to write this archive.
        """
        version = self.info().get('libraryVersionString')
        return re.search(r"\d.\d.\d", version).group(0)

    def using_version(self):
        """
        Returns the version of alembic used to read this archive.
        """
        return alembic.Abc.GetLibraryVersionShort()

    def type(self):
        """Returns "Archive"."""
        return self.__class__.__name__

    def path(self):
        """Returns the filepath for this Archive."""
        return self.filepath

    def is_leaf(self):
        """Returns False."""
        return False

    @property
    def name(self):
        """Returns the basename of this archive."""
        return os.path.basename(self.filepath)

    @property
    def timesamplings(self):
        """
        Generator that yields tuples of (index, TimeSampling) objects.
        """
        if self.iobject:
            iarch = self.iobject
            num_samples = iarch.getNumTimeSamplings()
            return [iarch.getTimeSampling(i) for i in range(num_samples)]
        return []

    def time_range(self):
        """
        Returns a tuple of the global start and end time in seconds.

        ** Depends on the X.samples property being set on the Top node,
        which is currently being written by Maya only. **
        """
        top_props = self.top.properties
        g_start_frame, g_end_time = (None, None)
        start_time, end_time = (0, 0)
        for index, tsamp in enumerate(self.timesamplings):
            prop_name = "%s.samples" % index
            if prop_name not in top_props:
                continue
            num_samples = top_props[prop_name].get_value()
            start_time = tsamp.getSampleTime(0)
            end_time = tsamp.getSampleTime(num_samples - 1)
            if g_start_frame is None:
                g_start_frame = start_time
            else:
                min(start_time, g_start_frame)
            if g_end_time is None:
                g_end_time = end_time
            else:
                max(end_time, g_end_time)
        return (start_time, end_time)

    def start_time(self):
        """Returns the global start time in seconds."""
        return self.time_range()[0]

    def start_frame(self):
        """
        :param fps: Frames per second used to calculate the start frame
        :return: Start frame as int
        """
        return round(self.start_time() * self.fps)

    def end_time(self):
        """Returns the global end time in seconds."""
        return self.time_range()[1]

    def end_frame(self):
        """
        :param fps: Frames per second used to calculate the end frame
        :return: Last frame as int
        """
        return round(self.end_time() * self.fps)

    def close(self):
        """Closes this archive and makes it immutable."""
        def close_tree(obj):
            """recursive close"""
            obj.close()
            for child in obj.children.values():
                close_tree(child)
        for child in self.top.children.values():
            close_tree(child)
        self.oobject = None
        self.top.oobject = None

    def __write(self):
        """
        Recursively calls save() on object hierarchy. Normally, you will
        want to call write_to_file instead.
        """
        if not self.oobject:
            raise ValueError("No output filepath specified")
        self.top.save()
        def save_tree(obj):
            """recursive save"""
            obj.save()
            for child in obj.children.values():
                save_tree(child)
        for child in self.top.children.values():
            save_tree(child)
        self.top.close()

    def write_to_file(self, filepath=None):
        """
        Writes this archive to a file on disk and closes the Archive.
        """
        smps = []
        if self.iobject and not self.oobject:
            smps = [(i, ts) for i, ts in enumerate(self.timesamplings) if i > 0]
        if not self.oobject:
            self.oobject = alembic.Abc.OArchive(filepath)
            self.top.oobject = self.oobject.getTop()
        for i, time_sample in smps:
            self.oobject.addTimeSampling(time_sample)
        self.__write()
        self.close()

class Property(object):
    """Property I/O Object."""
    def __init__(self, iproperty=None, time_sampling_id=0, name=None):
        """
        :param iproperty: Alembic IProperty class object.
        :param time_sampling_id: TimeSampling object ID (inherits down).
        """
        super(Property, self).__init__()
        
        # init some private variables
        self._parent = None
        self._name = name
        self._iobject = iproperty
        self._oobject = None
        self._klass = None
        self._values = []
        self._prop_dict = DeepDict(self)
        self.time_sampling_id = time_sampling_id

        # if we have an iproperty, get some values from it
        if iproperty:
            self.__read_property(iproperty)

    def __repr__(self):
        return '<Property "%s">' % self.name

    def get_item(self, item):
        """used for deep dict access"""
        return self.properties[item]
   
    def set_item(self, name, item):
        """used for deep dict access"""
        self.properties[name] = item

    def __get_iobject(self):
        """gets iproperty"""
        return self._iobject

    def __set_iobject(self, iobject):
        """sets iproperty"""
        self._iobject = iobject

    iobject = property(__get_iobject, __set_iobject, 
                       doc="Internal Alembic IProperty object.")

    def __get_oobject(self):
        """sets oproperty"""
        _parent = None
        if not self._oobject and self.parent:
            meta = alembic.AbcCoreAbstract.MetaData()
            if self.is_compound() and not self._klass:
                self._klass = alembic.Abc.OCompoundProperty
            elif self._klass is None and len(self.values) > 0:
                self._klass = get_simple_oprop_class(self.name, self.values[0])
            if self.is_compound() and self.iobject:
                meta.set('schema', self.iobject.getMetaData().get('schema'))
            if type(self.parent) == Property and self.parent.is_compound():
                _parent = self.parent.oobject
            else:
                if hasattr(self.parent.oobject, 'getProperties'):
                    _parent = self.parent.oobject.getProperties()
            if _parent and _parent.getPropertyHeader(self.name):
                # pre-existing property exists, see Property.__get_oobject
                pass
            elif _parent and self._klass:
                self._oobject = self._klass(_parent, 
                                            self.name, 
                                            meta, 
                                            self.time_sampling_id)
        return self._oobject

    def __set_oobject(self, oobject):
        """sets oproperty"""
        self._oobject = oobject

    oobject = property(__get_oobject, __set_oobject, 
                       doc="Internal Alembic OProperty object.")

    def __get_parent(self):
        """gets parent"""
        if self._parent is None and self.iobject:
            self._parent = wrap(self.iobject.getParent())
        return self._parent

    def __set_parent(self, parent):
        """sets parent"""
        self._parent = parent

    parent = property(__get_parent, __set_parent, 
                      doc="Parent object or property.")

    def __get_name(self):
        """gets name"""
        if not self._name:
            if self.iobject:
                self._name = self.iobject.getName()
            else:
                self._name = None
        return self._name

    def __set_name(self, name):
        """sets name"""
        old = self._name
        self._name = name
        if self._parent and hasattr(self._parent, "_prop_dict"):
            if old and old in self.parent.properties.keys():
                self._parent.properties.remove(old)
                self._parent.properties[name] = self

    name = property(__get_name, __set_name, 
                    doc="Gets and sets the property name.")

    def type(self):
        """Returns the name of the class."""
        if self.is_compound():
            return "Compound Property"
        return self.__class__.__name__

    def archive(self):
        """Returns the Archive for this property."""
        parent = self.parent
        while parent and parent.type() != "Archive":
            parent = parent.parent
        return parent

    def path(self):
        """Returns the full path/name of this property."""
        path = [""]
        obj = self
        while obj and obj.type() != "Top":
            path.insert(1, obj.name)
            obj = obj.parent
        return "/".join(path)

    def object(self):
        """Returns the object parent for this property."""
        obj = self.parent
        while obj and "Property" in obj.type():
            obj = obj.parent
        return obj

    def add_property(self, prop):
        """
        Add a property to this, making this property a compound property.

        :param property: cask.Property class object.
        """
        if len(self.values) > 0:
            raise TypeError("Properties with values cannot have sub-properies")
        self.properties[prop.name] = prop

    def __read_property(self, iproperty=None):
        """
        Sets the internal IProperty object.

        :param iproperty: Alembic IProperty object.
        """
        if iproperty:
            self.iobject = iproperty
            self.name = iproperty.getName()
        if iproperty.isCompound():
            for i in range(self.iobject.getNumProperties()):
                self.add_property(Property(iproperty.getProperty(i)))

    @property
    def properties(self):
        """Child properties accessor."""
        return self._prop_dict

    def is_leaf(self):
        """
        Returns True if this property is a leaf node, i.e. it has no sub-properties.
        """
        return len(self.properties) == 0

    def is_compound(self):
        """
        Returns True if this property contains sub-properties.

        Note that compound properties cannot have values, and
        simple properties cannont have sub-properties.
        """
        if self.iobject:
            return self.iobject.isCompound()
        return len(self.properties) > 0

    def __get_sample_index(self, time=None, frame=None):
        """
        Converts time in secs or frame number to sample index.

        :param time: time in seconds.
        :param frame: frame number.
        :return: sample index.
        """
        if len(self.properties) > 0:
            raise TypeError(_COMPOUND_PROPERTY_VALUE_ERROR_)
        ts = self.object().schema.getTimeSampling()
        numSamples = self.object().schema.getNumSamples()
        if time is not None:
            return ts.getNearIndex(float(time), numSamples)
        elif frame is not None:
            return ts.getNearIndex((frame / self.archive().fps), numSamples)
        else:
            return 0

    @property
    def values(self):
        """
        Returns dictionary of values stored on this property.
        """
        if not self.is_compound() and not self._values and self.iobject:
            for i in range(len(self.iobject.samples)):
                try:
                    self._values.insert(i, self.iobject.samples[i])
                except RuntimeError, err:
                    print "Bad value on sample:", i, err
                    self._values.insert(i, str(err))
        return self._values

    def get_value(self, index=None, time=None, frame=None):
        """
        Returns a the value stored on this property for a given sample
        index, time or frame.

        Provide one of the following args. If none are provided, it will
        return the 0th value.

        :param index: sample index
        :param time: time in seconds
        :param frame: frame number (assumes 24fps, to change set on archive)
        """
        if self.is_compound():
            raise TypeError(_COMPOUND_PROPERTY_VALUE_ERROR_)
        if index == None and time == None and frame == None:
            index = 0
        elif index is None:
            index = self.__get_sample_index(time, frame)
        try:
            return self.values[index]
        except (KeyError, IndexError):
            val = self.iobject.getValue(index)
            self.values[index] = val
            return val

    def set_value(self, value, index=None, time=None, frame=None):
        """
        Sets a value on the property at a given index.

        Provide one of the following args. If none are provided, it will
        append to the end.

        :param index: sample index
        :param time: time in seconds
        :param frame: frame number (assumes 24fps, to change set on archive)
        """
        if self.is_compound():
            raise TypeError(_COMPOUND_PROPERTY_VALUE_ERROR_)
        value = _delist(value)
        if index == None and time == None and frame == None:
            index = len(self._values)
        elif index is None:
            index = self.__get_sample_index(time, frame)
        if self._klass is None:
            self._klass = get_simple_oprop_class(self.name, value)
        if self._klass == alembic.Abc.OFloatArrayProperty:
            farray = imath.FloatArray(len(value))
            for i, val in enumerate(value):
                farray[i] = val
            value = farray
        elif self._klass == alembic.Abc.OC3fProperty:
            value = imath.Color3f(value[0], value[1], value[2])
        elif self._klass == alembic.Abc.OFloatProperty:
            value = float(value)
        elif self._klass == alembic.Abc.OStringProperty:
            value = str(value)
        if type(value) == imath.UnsignedIntArray:
            value = [int(val) for val in value]
        if index < len(self.values):
            self.values[index] = value
        else:
            self.values.append(value)

    def clear_properties(self):
        """Clears the properties container."""
        self._prop_dict = DeepDict(self)

    def clear_values(self):
        """Clears the values container."""
        self._values = []

    def close(self):
        """
        Closes this property by removing references to internal OProperty.
        """
        self.oobject = None
        for prop in self.properties.values():
            prop.close()

    def save(self):
        """
        Walks sub-tree and creates corresponding alembic OProperty classes,
        if they don't exist, and sets values.
        """
        if self.oobject and not self.is_compound():
            for value in self.values:
                try:
                    self.oobject.setValue(value)
                except TypeError, err:
                    print "Error setting", value, "on", self.name, "\n", err
        else:
            for prop in self.properties.values():
                prop.parent = self
                prop.save()

class Object(object):
    """Base I/O Object class."""
    __sample_class = None
    def __init__(self, iobject=None, schema=None, 
                 time_sampling_id=None, name=None):
        """
        :param iobject: Any alembic.Abc.IObject subclass object
        :param schema: Any alembic.Abc.ISchema subclass object
        :param time_sampling_id: The ID of the TimeSampling object
        """
        super(Object, self).__init__()

        # init some private variables
        self._name = name
        self._metadata = {}
        self._isamples = []
        self._osamples = []
        self._iobject = iobject
        self._oobject = None
        self._klass = None
        self._schema = schema
        self._parent = None
        self._is_animated = None
        self._tsid = time_sampling_id
        self._prop_dict = DeepDict(self)
        self._child_dict = DeepDict(self)

        # init some stuff
        self.clear_properties()
        self.clear_samples()
        self.clear_children()
        self.__read_object()

    def __repr__(self):
        return '<%s "%s">' % (self.__class__.__name__, self.name)

    def get_item(self, item):
        """used for deep dict access"""
        return self.children[item]
    
    def set_item(self, name, item):
        """used for deep dict access"""
        self.children[name] = item
    
    @property
    def __sample_methods(self):
        """gets this object's sample methods"""
        return dir(self.__sample_class)

    def __get_iobject(self):
        """gets iobject"""
        return self._iobject

    def __set_iobject(self, iobject):
        """sets iobject"""
        self._iobject = iobject

    iobject = property(__get_iobject, __set_iobject, 
                       doc="Internal Alembic IObject object.")

    def __get_oobject(self):
        """gets oobject"""
        if self._oobject is None:
            meta = alembic.AbcCoreAbstract.MetaData()
            # Using OObject subclasses (like OXform) automatically
            # creates hidden Compound Properties (like .xform) which
            # results in name collisions when saving properties in cask.
            # Using OObjects avoids this problem, but we have to set 
            # the metadata manually.
            if self.iobject:
                self._klass = alembic.Abc.OObject
                meta.set('schema', self.iobject.getMetaData().get('schema'))
                meta.set('schemaObjTitle', 
                         self.iobject.getMetaData().get('schemaObjTitle'))
                meta.set('schemaBaseType', 
                         self.iobject.getMetaData().get('schemaBaseType'))
            else:
                self._klass = OOBJECTS.get(self.type())
            if self._klass:
                self._oobject = self._klass(self.parent.oobject, self.name, 
                                            meta, self.time_sampling_id)
        return self._oobject

    def __set_oobject(self, oobject):
        """sets oobject"""
        self._oobject = oobject

    oobject = property(__get_oobject, __set_oobject, 
                       doc="Internal Alembic OObject object.")

    @wrapped
    def __get_schema(self):
        """gets schema"""
        if self.iobject and self._schema is None:
            self._schema = self.iobject.getSchema()
        return self._schema

    def __set_schema(self, schema):
        """sets schema"""
        self._schema = schema

    schema = property(__get_schema, __set_schema, 
                      doc="Returns the Alembic schema object.")

    @classmethod
    def matches(cls, iobject):
        """
        Returns True if a given iobject type matches this type. 
        """
        return IOBJECTS.get(cls.__name__).matches(iobject.getMetaData())

    def __get_parent(self):
        """gets parent"""
        if self._parent is None and self.iobject:
            parent = self.iobject.getParent()
            if parent.getFullName() == "/":
                self._parent = Top(parent)
            else:
                self._parent = wrap(parent)
        return self._parent

    def __set_parent(self, parent):
        """sets parent"""
        self._parent = parent
        self._oobject = None
        if parent and type(self) != Top:
            parent.add_child(self)

    parent = property(__get_parent, __set_parent, 
                      doc="Parent object accessor.")

    def __get_name(self):
        """gets name"""
        if not hasattr(self, "_name"):
            if self.iobject:
                self._name = self.iobject.getName()
            else:
                self._name = None
        return self._name

    def __set_name(self, name):
        """sets name"""
        old = self._name
        self._name = name
        if self.parent and hasattr(self._parent, "_child_dict"):
            if old and old in self._parent._child_dict.keys():
                self._parent._child_dict.remove(old)
                self._parent._child_dict[name] = self

    name = property(__get_name, __set_name, 
                    doc="Set and get the name of the object.")

    def __get_tsid(self):
        """gets time sampling id"""
        if self._tsid is None:
            return self.parent.time_sampling_id
        return self._tsid

    def __set_tsid(self, tsid):
        """sets time sampling id"""
        self._tsid = tsid

    time_sampling_id = property(__get_tsid, __set_tsid, 
                                doc="Time sampling ID.")

    def __get_metadata(self):
        """returns metadata dict"""
        if not self._metadata and self.iobject:
            meta = self.iobject.getMetaData()
            for field in meta.serialize().split(';'):
                splits = field.split('=')
                key = splits[0].replace('_ai_','')
                value = '='.join(splits[1:])
                self._metadata[key] = value
        return self._metadata

    def __set_metadata(self, metadata):
        """sets metadata dict"""
        self._metadata = metadata

    metadata = property(__get_metadata, __set_metadata, 
                        doc="Metadata as a dict.")

    def archive(self):
        """Returns the Archive for this object."""
        parent = self.parent
        while parent and parent.type() != "Archive":
            parent = parent.parent
        return parent

    def path(self):
        """Returns the full path/name of this object."""
        path = [""]
        obj = self
        while obj and obj.type() != "Top":
            path.insert(1, obj.name)
            obj = obj.parent
        return "/".join(path)

    def type(self):
        """Returns the name of the class."""
        return self.__class__.__name__

    def add_child(self, child):
        """
        Adds a child object to this object.

        :param child: cask.Object
        """
        self.children[child.name] = child

    def __read_object(self):
        """reads object, sets name"""
        if self.iobject and type(self) != Top:
            self.name = self.iobject.getName()

    @property
    def children(self):
        """Returns children sub-tree accessor. """
        if not self._child_dict and self.iobject:
            for i in range(self.iobject.getNumChildren()):
                child = wrap(self.iobject.getChild(i))
                self._child_dict[child.name] = child
        return self._child_dict

    @property
    def properties(self):
        """Properties accessor."""
        if not self._prop_dict and self.iobject:
            props = self.iobject.getProperties()
            for i in range(len(props.propertyheaders)):
                prop = Property(props.getProperty(i))
                self._prop_dict[prop.name] = prop
        return self._prop_dict

    @property
    def samples(self):
        """Returns samples from the Alembic IObject."""
        if self.iobject and len(self._isamples) == 0:
            num_samples = self.schema.getNumSamples()
            schema = self.schema
            self._isamples = [schema.getValue(i) for i in range(num_samples)]
        return self._isamples

    def set_sample(self, sample, index=None):
        """
        Sets an Alembic sample object on this object.

        *Do we want to expose samples at all? Should all data
        be set via seting values on properties, directly or with
        high level methods? 

        :param sample: Alembic sample object.
        :param index: Index of the sample to set, or None.
        """
        if index is None:
            index = len(self._osamples)
        self._osamples.insert(index, sample)

    def is_leaf(self):
        """
        Returns True if this object is a leaf node, i.e. it has no children.
        """
        return len(self.children) == 0

    def is_animated(self):
        """
        Returns True if any properties are not constant.
        """
        self._is_animated = False
        def _is_animated(prop):
            """recursive check"""
            if not prop.is_compound() and not prop.iobject.isConstant():
                self._is_animated = True
            for child in prop.properties.values():
                _is_animated(child)
        for prop in self.properties.values():
            _is_animated(prop)
        return self._is_animated

    def is_deforming(self):
        """
        Returns True if the object has changing P values.
        """
        prop = self.properties.get("P")
        if prop:
            return prop.isConstant()
        return False

    def start_frame(self):
        """
        :param fps: Frames per second used to calculate the start frame 
        (default 24.0)

        :return: Start frame as float
        """
        try:
            time_sample = self.iobject.getTimeSampling()
            fps = self.archive().fps
            return round(time_sample.getSampleTime(0) * fps)
        except AttributeError:
            return self.parent.start_frame()

    def end_frame(self, fps=24):
        """
        :param fps: Frames per second used to calculate the end frame 
        (default 24.0)

        :return: Last frame as float
        """
        try:
            time_sample = self.iobject.getTimeSampling()
            num_samples = self.iobject.getNumSamples()
            fps = self.archive().fps
            if num_samples:
                return round(time_sample.getSampleTime(num_samples - 1) * fps)
            return round(time_sample.getSampleTime(0) * fps)
        except AttributeError:
            return self.parent.end_frame()

    def global_matrix(self):
        """Returns world space matrix for this object."""
        def accum_xform(xform, obj):
            """recursive xform accum"""
            if Xform.matches(obj._iobject):
                xform *= obj.matrix()
        xform = imath.M44d()
        xform.makeIdentity()
        parent = self
        while parent and type(parent) not in [Archive, Top]:
            accum_xform(xform, parent)
            parent = parent.parent
        return xform

    def clear_properties(self):
        """Clears the internal properties container."""
        self._prop_dict = DeepDict(self)

    def clear_samples(self):
        """Clears the internal samples container."""
        self._isamples = []
        self._osamples = []

    def clear_children(self):
        """Clears the internal children container."""
        self._child_dict = DeepDict(self)

    def close(self):
        """
        Closes this object by removing references to internal OObject.
        """
        self.oobject = None
        for prop in self.properties.values():
            prop.close()

    def save(self):
        """
        Walks child and property sub-trees creating OObjects as necessary.
        """
        obj = self.oobject
        for prop in self.properties.values():
            prop.save()
        for sample in self._osamples:
            obj.getSchema().set(sample)

class Top(Object):
    """Alembic Top Object."""
    def __init__(self, archive, iobject=None):
        super(Top, self).__init__(iobject)
        self.parent = archive
        self.parent.top = self
        self.oobject = None

    @classmethod
    def matches(cls, iobject):
        """Returns True if iobject is a Top object."""
        return iobject.__class__ == cls.__class__

    def is_leaf(self):
        """Returns False."""
        return False

    def path(self):
        """Returns the full path/name of this object."""
        return "/"

    def __get_name(self):
        return "ABC"

    def __set_name(self, name):
        raise TypeError("Can not set name on Top object.")

    name = property(__get_name, __set_name, 
                    doc="Returns the object name, which for Top is always ABC")

class Xform(Object):
    """Xform I/O Object subclass."""
    __sample_class = alembic.AbcGeom.XformSample
    def __init__(self, *args, **kwargs):
        super(Xform, self).__init__(*args, **kwargs)

    def matrix(self, index=0):
        """
        Returns the xform matrix value for a given index.

        :param index: Sample index.
        """
        return self.schema.getValue(index).getMatrix()

    def set_scale(self, *args):
        """
        Creates an internal XformSample object and sets the scale value.
        """
        if len(args) == 1 and type(args[0]) == imath.V3d:
            scale = args[0]
        else:
            scale = imath.V3d(args[0], args[1], args[2])
        xform_sample = alembic.AbcGeom.XformSample()
        xform_sample.setScale(scale)
        self.set_sample(xform_sample)

class PolyMesh(Object):
    """PolyMesh I/O Object subclass."""
    __sample_class = alembic.AbcGeom.OPolyMeshSchemaSample
    def __init__(self, *args, **kwargs):
        super(PolyMesh, self).__init__(*args, **kwargs)

class SubD(Object):
    """SubD I/O Object subclass."""
    __sample_class = alembic.AbcGeom.OSubDSchemaSample
    def __init__(self, *args, **kwargs):
        super(SubD, self).__init__(*args, **kwargs)

class FaceSet(Object):
    """FaceSet I/O Object subclass."""
    __sample_class = alembic.AbcGeom.OFaceSetSchemaSample
    def __init__(self, *args, **kwargs):
        super(FaceSet, self).__init__(*args, **kwargs)

class Curve(Object):
    """Curve I/O Object subclass."""
    __sample_class = alembic.AbcGeom.OCurvesSchemaSample
    def __init__(self, *args, **kwargs):
        super(Curve, self).__init__(*args, **kwargs)

class Camera(Object):
    """Camera I/O Object subclass."""
    def __init__(self, *args, **kwargs):
        super(Camera, self).__init__(*args, **kwargs)

class NuPatch(Object):
    """NuPath I/O Object subclass."""
    __sample_class = alembic.AbcGeom.ONuPatchSchemaSample
    def __init__(self, *args, **kwargs):
        super(NuPatch, self).__init__(*args, **kwargs)

class Material(Object):
    """Material I/O Object subclass."""
    def __init__(self, *args, **kwargs):
        super(Material, self).__init__(*args, **kwargs)

class Light(Object):
    """Light I/O Object subclass."""
    def __init__(self, *args, **kwargs):
        super(Light, self).__init__(*args, **kwargs)
