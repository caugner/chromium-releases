# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import copy
import json
import logging
import os
from collections import defaultdict, Mapping

import svn_constants
import third_party.json_schema_compiler.json_parse as json_parse
import third_party.json_schema_compiler.model as model
import third_party.json_schema_compiler.idl_schema as idl_schema
import third_party.json_schema_compiler.idl_parser as idl_parser
from third_party.handlebar import Handlebar


def _RemoveNoDocs(item):
  if json_parse.IsDict(item):
    if item.get('nodoc', False):
      return True
    for key, value in item.items():
      if _RemoveNoDocs(value):
        del item[key]
  elif type(item) == list:
    to_remove = []
    for i in item:
      if _RemoveNoDocs(i):
        to_remove.append(i)
    for i in to_remove:
      item.remove(i)
  return False


def _DetectInlineableTypes(schema):
  '''Look for documents that are only referenced once and mark them as inline.
  Actual inlining is done by _InlineDocs.
  '''
  if not schema.get('types'):
    return

  ignore = frozenset(('value', 'choices'))
  refcounts = defaultdict(int)
  # Use an explicit stack instead of recursion.
  stack = [schema]

  while stack:
    node = stack.pop()
    if isinstance(node, list):
      stack.extend(node)
    elif isinstance(node, Mapping):
      if '$ref' in node:
        refcounts[node['$ref']] += 1
      stack.extend(v for k, v in node.iteritems() if k not in ignore)

  for type_ in schema['types']:
    if not 'noinline_doc' in type_:
      if refcounts[type_['id']] == 1:
        type_['inline_doc'] = True


def _InlineDocs(schema):
  '''Replace '$ref's that refer to inline_docs with the json for those docs.
  '''
  types = schema.get('types')
  if types is None:
    return

  inline_docs = {}
  types_without_inline_doc = []

  # Gather the types with inline_doc.
  for type_ in types:
    if type_.get('inline_doc'):
      inline_docs[type_['id']] = type_
      for k in ('description', 'id', 'inline_doc'):
        type_.pop(k, None)
    else:
      types_without_inline_doc.append(type_)
  schema['types'] = types_without_inline_doc

  def apply_inline(node):
    if isinstance(node, list):
      for i in node:
        apply_inline(i)
    elif isinstance(node, Mapping):
      ref = node.get('$ref')
      if ref and ref in inline_docs:
        node.update(inline_docs[ref])
        del node['$ref']
      for k, v in node.iteritems():
        apply_inline(v)

  apply_inline(schema)


def _CreateId(node, prefix):
  if node.parent is not None and not isinstance(node.parent, model.Namespace):
    return '-'.join([prefix, node.parent.simple_name, node.simple_name])
  return '-'.join([prefix, node.simple_name])


def _FormatValue(value):
  '''Inserts commas every three digits for integer values. It is magic.
  '''
  s = str(value)
  return ','.join([s[max(0, i - 3):i] for i in range(len(s), 0, -3)][::-1])


def _GetAddRulesDefinitionFromEvents(events):
  '''Parses the dictionary |events| to find the definition of the method
  addRules among functions of the type Event.
  '''
  assert 'types' in events, \
      'The dictionary |events| must contain the key "types".'
  event_list = [t for t in events['types']
                if 'name' in t and t['name'] == 'Event']
  assert len(event_list) == 1, 'Exactly one type must be called "Event".'
  event = event_list[0]
  assert 'functions' in event, 'The type Event must contain "functions".'
  result_list = [f for f in event['functions']
                 if 'name' in f and f['name'] == 'addRules']
  assert len(result_list) == 1, \
      'Exactly one function must be called "addRules".'
  return result_list[0]


class _JSCModel(object):
  '''Uses a Model from the JSON Schema Compiler and generates a dict that
  a Handlebar template can use for a data source.
  '''

  def __init__(self,
               json,
               ref_resolver,
               disable_refs,
               availability_finder,
               branch_utility,
               parse_cache,
               template_data_source,
               add_rules_schema_function,
               idl=False):
    self._ref_resolver = ref_resolver
    self._disable_refs = disable_refs
    self._availability_finder = availability_finder
    self._branch_utility = branch_utility
    self._api_availabilities = parse_cache.GetFromFile(
        '%s/api_availabilities.json' % svn_constants.JSON_PATH)
    self._intro_tables = parse_cache.GetFromFile(
        '%s/intro_tables.json' % svn_constants.JSON_PATH)
    self._api_features = parse_cache.GetFromFile(
        '%s/_api_features.json' % svn_constants.API_PATH)
    self._template_data_source = template_data_source
    self._add_rules_schema_function = add_rules_schema_function
    clean_json = copy.deepcopy(json)
    if _RemoveNoDocs(clean_json):
      self._namespace = None
    else:
      if idl:
        _DetectInlineableTypes(clean_json)
      _InlineDocs(clean_json)
      self._namespace = model.Namespace(clean_json, clean_json['namespace'])

  def _FormatDescription(self, description):
    if self._disable_refs:
      return description
    return self._ref_resolver.ResolveAllLinks(description,
                                              namespace=self._namespace.name)

  def _GetLink(self, link):
    if self._disable_refs:
      type_name = link.split('.', 1)[-1]
      return { 'href': '#type-%s' % type_name, 'text': link, 'name': link }
    return self._ref_resolver.SafeGetLink(link, namespace=self._namespace.name)

  def ToDict(self):
    if self._namespace is None:
      return {}
    as_dict = {
      'name': self._namespace.name,
      'types': self._GenerateTypes(self._namespace.types.values()),
      'functions': self._GenerateFunctions(self._namespace.functions),
      'events': self._GenerateEvents(self._namespace.events),
      'properties': self._GenerateProperties(self._namespace.properties),
      'introList': self._GetIntroTableList(),
      'channelWarning': self._GetChannelWarning(),
      'byName': {},
    }
    # Make every type/function/event/property also accessible by name for
    # rendering specific API entities rather than the whole thing at once, for
    # example {{apis.manifestTypes.byName.ExternallyConnectable}}.
    for item_type in ('types', 'functions', 'events', 'properties'):
      as_dict['byName'].update(
          (item['name'], item) for item in as_dict[item_type])
    return as_dict

  def _GetApiAvailability(self):
    # Check for a predetermined availability for this API.
    api_info = self._api_availabilities.get(self._namespace.name)
    if api_info is not None:
      channel = api_info['channel']
      if channel == 'stable':
        return self._branch_utility.GetStableChannelInfo(api_info['version'])
      return self._branch_utility.GetChannelInfo(channel)
    return self._availability_finder.GetApiAvailability(self._namespace.name)

  def _GetChannelWarning(self):
    if not self._IsExperimental():
      return { self._GetApiAvailability().channel: True }
    return None

  def _IsExperimental(self):
    return self._namespace.name.startswith('experimental')

  def _GenerateTypes(self, types):
    return [self._GenerateType(t) for t in types]

  def _GenerateType(self, type_):
    type_dict = {
      'name': type_.simple_name,
      'description': self._FormatDescription(type_.description),
      'properties': self._GenerateProperties(type_.properties),
      'functions': self._GenerateFunctions(type_.functions),
      'events': self._GenerateEvents(type_.events),
      'id': _CreateId(type_, 'type')
    }
    self._RenderTypeInformation(type_, type_dict)
    return type_dict

  def _GenerateFunctions(self, functions):
    return [self._GenerateFunction(f) for f in functions.values()]

  def _GenerateFunction(self, function):
    function_dict = {
      'name': function.simple_name,
      'description': self._FormatDescription(function.description),
      'callback': self._GenerateCallback(function.callback),
      'parameters': [],
      'returns': None,
      'id': _CreateId(function, 'method')
    }
    if (function.parent is not None and
        not isinstance(function.parent, model.Namespace)):
      function_dict['parentName'] = function.parent.simple_name
    if function.returns:
      function_dict['returns'] = self._GenerateType(function.returns)
    for param in function.params:
      function_dict['parameters'].append(self._GenerateProperty(param))
    if function.callback is not None:
      # Show the callback as an extra parameter.
      function_dict['parameters'].append(
          self._GenerateCallbackProperty(function.callback))
    if len(function_dict['parameters']) > 0:
      function_dict['parameters'][-1]['last'] = True
    return function_dict

  def _GenerateEvents(self, events):
    return [self._GenerateEvent(e) for e in events.values()]

  def _GenerateEvent(self, event):
    event_dict = {
      'name': event.simple_name,
      'description': self._FormatDescription(event.description),
      'filters': [self._GenerateProperty(f) for f in event.filters],
      'conditions': [self._GetLink(condition)
                     for condition in event.conditions],
      'actions': [self._GetLink(action) for action in event.actions],
      'supportsRules': event.supports_rules,
      'supportsListeners': event.supports_listeners,
      'id': _CreateId(event, 'event')
    }
    if (event.parent is not None and
        not isinstance(event.parent, model.Namespace)):
      event_dict['parentName'] = event.parent.simple_name
    # For the addRules method we can use the common definition, because addRules
    # has the same signature for every event.
    if event.supports_rules:
      event_dict['addRulesFunction'] = self._add_rules_schema_function()
    # We need to create the method description for addListener based on the
    # information stored in |event|.
    if event.supports_listeners:
      callback_object = model.Function(parent=event,
                                       name='callback',
                                       json={},
                                       namespace=event.parent,
                                       origin='')
      callback_object.params = event.params
      if event.callback:
        callback_object.callback = event.callback
      callback_parameters = self._GenerateCallbackProperty(callback_object)
      callback_parameters['last'] = True
      event_dict['addListenerFunction'] = {
        'name': 'addListener',
        'callback': self._GenerateFunction(callback_object),
        'parameters': [callback_parameters]
      }
    return event_dict

  def _GenerateCallback(self, callback):
    if not callback:
      return None
    callback_dict = {
      'name': callback.simple_name,
      'simple_type': {'simple_type': 'function'},
      'optional': callback.optional,
      'parameters': []
    }
    for param in callback.params:
      callback_dict['parameters'].append(self._GenerateProperty(param))
    if (len(callback_dict['parameters']) > 0):
      callback_dict['parameters'][-1]['last'] = True
    return callback_dict

  def _GenerateProperties(self, properties):
    return [self._GenerateProperty(v) for v in properties.values()]

  def _GenerateProperty(self, property_):
    if not hasattr(property_, 'type_'):
      for d in dir(property_):
        if not d.startswith('_'):
          print ('%s -> %s' % (d, getattr(property_, d)))
    type_ = property_.type_

    # Make sure we generate property info for arrays, too.
    # TODO(kalman): what about choices?
    if type_.property_type == model.PropertyType.ARRAY:
      properties = type_.item_type.properties
    else:
      properties = type_.properties

    property_dict = {
      'name': property_.simple_name,
      'optional': property_.optional,
      'description': self._FormatDescription(property_.description),
      'properties': self._GenerateProperties(type_.properties),
      'functions': self._GenerateFunctions(type_.functions),
      'parameters': [],
      'returns': None,
      'id': _CreateId(property_, 'property')
    }

    if type_.property_type == model.PropertyType.FUNCTION:
      function = type_.function
      for param in function.params:
        property_dict['parameters'].append(self._GenerateProperty(param))
      if function.returns:
        property_dict['returns'] = self._GenerateType(function.returns)

    if (property_.parent is not None and
        not isinstance(property_.parent, model.Namespace)):
      property_dict['parentName'] = property_.parent.simple_name

    value = property_.value
    if value is not None:
      if isinstance(value, int):
        property_dict['value'] = _FormatValue(value)
      else:
        property_dict['value'] = value
    else:
      self._RenderTypeInformation(type_, property_dict)

    return property_dict

  def _GenerateCallbackProperty(self, callback):
    property_dict = {
      'name': callback.simple_name,
      'description': self._FormatDescription(callback.description),
      'optional': callback.optional,
      'id': _CreateId(callback, 'property'),
      'simple_type': 'function',
    }
    if (callback.parent is not None and
        not isinstance(callback.parent, model.Namespace)):
      property_dict['parentName'] = callback.parent.simple_name
    return property_dict

  def _RenderTypeInformation(self, type_, dst_dict):
    dst_dict['is_object'] = type_.property_type == model.PropertyType.OBJECT
    if type_.property_type == model.PropertyType.CHOICES:
      dst_dict['choices'] = self._GenerateTypes(type_.choices)
      # We keep track of which == last for knowing when to add "or" between
      # choices in templates.
      if len(dst_dict['choices']) > 0:
        dst_dict['choices'][-1]['last'] = True
    elif type_.property_type == model.PropertyType.REF:
      dst_dict['link'] = self._GetLink(type_.ref_type)
    elif type_.property_type == model.PropertyType.ARRAY:
      dst_dict['array'] = self._GenerateType(type_.item_type)
    elif type_.property_type == model.PropertyType.ENUM:
      dst_dict['enum_values'] = []
      for enum_value in type_.enum_values:
        dst_dict['enum_values'].append({'name': enum_value})
      if len(dst_dict['enum_values']) > 0:
        dst_dict['enum_values'][-1]['last'] = True
    elif type_.instance_of is not None:
      dst_dict['simple_type'] = type_.instance_of.lower()
    else:
      dst_dict['simple_type'] = type_.property_type.name.lower()

  def _GetIntroTableList(self):
    '''Create a generic data structure that can be traversed by the templates
    to create an API intro table.
    '''
    intro_rows = [
      self._GetIntroDescriptionRow(),
      self._GetIntroAvailabilityRow()
    ] + self._GetIntroDependencyRows()

    # Add rows using data from intro_tables.json, overriding any existing rows
    # if they share the same 'title' attribute.
    row_titles = [row['title'] for row in intro_rows]
    for misc_row in self._GetMiscIntroRows():
      if misc_row['title'] in row_titles:
        intro_rows[row_titles.index(misc_row['title'])] = misc_row
      else:
        intro_rows.append(misc_row)

    return intro_rows

  def _GetIntroDescriptionRow(self):
    ''' Generates the 'Description' row data for an API intro table.
    '''
    return {
      'title': 'Description',
      'content': [
        { 'text': self._FormatDescription(self._namespace.description) }
      ]
    }

  def _GetIntroAvailabilityRow(self):
    ''' Generates the 'Availability' row data for an API intro table.
    '''
    if self._IsExperimental():
      status = 'experimental'
      version = None
    else:
      availability = self._GetApiAvailability()
      status = availability.channel
      version = availability.version
    return {
      'title': 'Availability',
      'content': [{
        'partial': self._template_data_source.get(
            'intro_tables/%s_message.html' % status),
        'version': version
      }]
    }

  def _GetIntroDependencyRows(self):
    # Devtools aren't in _api_features. If we're dealing with devtools, bail.
    if 'devtools' in self._namespace.name:
      return []
    feature = self._api_features.get(self._namespace.name)
    assert feature, ('"%s" not found in _api_features.json.'
                     % self._namespace.name)

    dependencies = feature.get('dependencies')
    if dependencies is None:
      return []

    def make_code_node(text):
      return { 'class': 'code', 'text': text }

    permissions_content = []
    manifest_content = []

    def categorize_dependency(dependency):
      context, name = dependency.split(':', 1)
      if context == 'permission':
        permissions_content.append(make_code_node('"%s"' % name))
      elif context == 'manifest':
        manifest_content.append(make_code_node('"%s": {...}' % name))
      elif context == 'api':
        transitive_dependencies = (
            self._api_features.get(name, {}).get('dependencies', []))
        for transitive_dependency in transitive_dependencies:
          categorize_dependency(transitive_dependency)
      else:
        raise ValueError('Unrecognized dependency for %s: %s' % (
            self._namespace.name, context))

    for dependency in dependencies:
      categorize_dependency(dependency)

    dependency_rows = []
    if permissions_content:
      dependency_rows.append({
        'title': 'Permissions',
        'content': permissions_content
      })
    if manifest_content:
      dependency_rows.append({
        'title': 'Manifest',
        'content': manifest_content
      })
    return dependency_rows

  def _GetMiscIntroRows(self):
    ''' Generates miscellaneous intro table row data, such as 'Permissions',
    'Samples', and 'Learn More', using intro_tables.json.
    '''
    misc_rows = []
    # Look up the API name in intro_tables.json, which is structured
    # similarly to the data structure being created. If the name is found, loop
    # through the attributes and add them to this structure.
    table_info = self._intro_tables.get(self._namespace.name)
    if table_info is None:
      return misc_rows

    for category in table_info.keys():
      content = copy.deepcopy(table_info[category])
      for node in content:
        # If there is a 'partial' argument and it hasn't already been
        # converted to a Handlebar object, transform it to a template.
        if 'partial' in node:
          node['partial'] = self._template_data_source.get(node['partial'])
      misc_rows.append({ 'title': category, 'content': content })
    return misc_rows


class _LazySamplesGetter(object):
  '''This class is needed so that an extensions API page does not have to fetch
  the apps samples page and vice versa.
  '''

  def __init__(self, api_name, samples):
    self._api_name = api_name
    self._samples = samples

  def get(self, key):
    return self._samples.FilterSamples(key, self._api_name)


class APIDataSource(object):
  '''This class fetches and loads JSON APIs from the FileSystem passed in with
  |compiled_fs_factory|, so the APIs can be plugged into templates.
  '''

  class Factory(object):
    def __init__(self,
                 compiled_fs_factory,
                 base_path,
                 availability_finder,
                 branch_utility):
      def create_compiled_fs(fn, category):
        return compiled_fs_factory.Create(fn, APIDataSource, category=category)

      self._json_cache = create_compiled_fs(
          lambda api_name, api: self._LoadJsonAPI(api, False),
          'json')
      self._idl_cache = create_compiled_fs(
          lambda api_name, api: self._LoadIdlAPI(api, False),
          'idl')

      # These caches are used if an APIDataSource does not want to resolve the
      # $refs in an API. This is needed to prevent infinite recursion in
      # ReferenceResolver.
      self._json_cache_no_refs = create_compiled_fs(
          lambda api_name, api: self._LoadJsonAPI(api, True),
          'json-no-refs')
      self._idl_cache_no_refs = create_compiled_fs(
          lambda api_name, api: self._LoadIdlAPI(api, True),
          'idl-no-refs')

      self._idl_names_cache = create_compiled_fs(self._GetIDLNames, 'idl-names')
      self._names_cache = create_compiled_fs(self._GetAllNames, 'names')

      self._base_path = base_path
      self._availability_finder = availability_finder
      self._branch_utility = branch_utility
      self._parse_cache = create_compiled_fs(
          lambda _, json: json_parse.Parse(json),
          'intro-cache')
      # These must be set later via the SetFooDataSourceFactory methods.
      self._ref_resolver_factory = None
      self._samples_data_source_factory = None

      # This caches the result of _LoadAddRulesSchema.
      self._add_rules_schema = None

    def SetSamplesDataSourceFactory(self, samples_data_source_factory):
      self._samples_data_source_factory = samples_data_source_factory

    def SetReferenceResolverFactory(self, ref_resolver_factory):
      self._ref_resolver_factory = ref_resolver_factory

    def SetTemplateDataSource(self, template_data_source_factory):
      # This TemplateDataSource is only being used for fetching template data.
      self._template_data_source = template_data_source_factory.Create(
          None, {})

    def Create(self, request, disable_refs=False):
      '''Create an APIDataSource. |disable_refs| specifies whether $ref's in
      APIs being processed by the |ToDict| method of _JSCModel follows $ref's
      in the API. This prevents endless recursion in ReferenceResolver.
      '''
      if self._samples_data_source_factory is None:
        # Only error if there is a request, which means this APIDataSource is
        # actually being used to render a page.
        if request is not None:
          logging.error('SamplesDataSource.Factory was never set in '
                        'APIDataSource.Factory.')
        samples = None
      else:
        samples = self._samples_data_source_factory.Create(request)
      if not disable_refs and self._ref_resolver_factory is None:
        logging.error('ReferenceResolver.Factory was never set in '
                      'APIDataSource.Factory.')
      return APIDataSource(self._json_cache,
                           self._idl_cache,
                           self._json_cache_no_refs,
                           self._idl_cache_no_refs,
                           self._names_cache,
                           self._idl_names_cache,
                           self._base_path,
                           samples,
                           disable_refs)

    def _LoadAddRulesSchema(self):
      """ All events supporting rules have the addRules method. We source its
      description from Event in events.json.
      """
      if self._add_rules_schema is None:
        self._add_rules_schema = _GetAddRulesDefinitionFromEvents(
            self._json_cache.GetFromFile('%s/events.json' % self._base_path))
      return self._add_rules_schema

    def _LoadJsonAPI(self, api, disable_refs):
      return _JSCModel(
          json_parse.Parse(api)[0],
          self._ref_resolver_factory.Create() if not disable_refs else None,
          disable_refs,
          self._availability_finder,
          self._branch_utility,
          self._parse_cache,
          self._template_data_source,
          self._LoadAddRulesSchema).ToDict()

    def _LoadIdlAPI(self, api, disable_refs):
      idl = idl_parser.IDLParser().ParseData(api)
      return _JSCModel(
          idl_schema.IDLSchema(idl).process()[0],
          self._ref_resolver_factory.Create() if not disable_refs else None,
          disable_refs,
          self._availability_finder,
          self._branch_utility,
          self._parse_cache,
          self._template_data_source,
          self._LoadAddRulesSchema,
          idl=True).ToDict()

    def _GetIDLNames(self, base_dir, apis):
      return self._GetExtNames(apis, ['idl'])

    def _GetAllNames(self, base_dir, apis):
      return self._GetExtNames(apis, ['json', 'idl'])

    def _GetExtNames(self, apis, exts):
      return [model.UnixName(os.path.splitext(api)[0]) for api in apis
              if os.path.splitext(api)[1][1:] in exts]

  def __init__(self,
               json_cache,
               idl_cache,
               json_cache_no_refs,
               idl_cache_no_refs,
               names_cache,
               idl_names_cache,
               base_path,
               samples,
               disable_refs):
    self._base_path = base_path
    self._json_cache = json_cache
    self._idl_cache = idl_cache
    self._json_cache_no_refs = json_cache_no_refs
    self._idl_cache_no_refs = idl_cache_no_refs
    self._names_cache = names_cache
    self._idl_names_cache = idl_names_cache
    self._samples = samples
    self._disable_refs = disable_refs

  def _GenerateHandlebarContext(self, handlebar_dict, path):
    handlebar_dict['samples'] = _LazySamplesGetter(path, self._samples)
    return handlebar_dict

  def _GetAsSubdirectory(self, name):
    if name.startswith('experimental_'):
      parts = name[len('experimental_'):].split('_', 1)
      if len(parts) > 1:
        parts[1] = 'experimental_%s' % parts[1]
        return '/'.join(parts)
      return '%s/%s' % (parts[0], name)
    return name.replace('_', '/', 1)

  def get(self, key):
    if key.endswith('.html') or key.endswith('.json') or key.endswith('.idl'):
      path, ext = os.path.splitext(key)
    else:
      path = key
    unix_name = model.UnixName(path)
    idl_names = self._idl_names_cache.GetFromFileListing(self._base_path)
    names = self._names_cache.GetFromFileListing(self._base_path)
    if unix_name not in names and self._GetAsSubdirectory(unix_name) in names:
      unix_name = self._GetAsSubdirectory(unix_name)

    if self._disable_refs:
      cache, ext = (
          (self._idl_cache_no_refs, '.idl') if (unix_name in idl_names) else
          (self._json_cache_no_refs, '.json'))
    else:
      cache, ext = ((self._idl_cache, '.idl') if (unix_name in idl_names) else
                    (self._json_cache, '.json'))
    return self._GenerateHandlebarContext(
        cache.GetFromFile('%s/%s%s' % (self._base_path, unix_name, ext)),
        path)
