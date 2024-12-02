// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/shared_impl/ppb_input_event_shared.h"

#include "ppapi/shared_impl/var.h"

using ppapi::thunk::PPB_InputEvent_API;

namespace ppapi {

InputEventData::InputEventData()
    : is_filtered(false),
      event_type(PP_INPUTEVENT_TYPE_UNDEFINED),
      event_time_stamp(0.0),
      event_modifiers(0),
      mouse_button(PP_INPUTEVENT_MOUSEBUTTON_NONE),
      mouse_position(PP_MakePoint(0, 0)),
      mouse_click_count(0),
      mouse_movement(PP_MakePoint(0, 0)),
      wheel_delta(PP_MakeFloatPoint(0.0f, 0.0f)),
      wheel_ticks(PP_MakeFloatPoint(0.0f, 0.0f)),
      wheel_scroll_by_page(false),
      key_code(0),
      character_text(),
      composition_target_segment(-1),
      composition_selection_start(0),
      composition_selection_end(0) {
}

InputEventData::~InputEventData() {
}

PPB_InputEvent_Shared::PPB_InputEvent_Shared(ResourceObjectType type,
                                             PP_Instance instance,
                                             const InputEventData& data)
    : Resource(type, instance),
      data_(data) {
}

PPB_InputEvent_API* PPB_InputEvent_Shared::AsPPB_InputEvent_API() {
  return this;
}

const InputEventData& PPB_InputEvent_Shared::GetInputEventData() const {
  return data_;
}

PP_InputEvent_Type PPB_InputEvent_Shared::GetType() {
  return data_.event_type;
}

PP_TimeTicks PPB_InputEvent_Shared::GetTimeStamp() {
  return data_.event_time_stamp;
}

uint32_t PPB_InputEvent_Shared::GetModifiers() {
  return data_.event_modifiers;
}

PP_InputEvent_MouseButton PPB_InputEvent_Shared::GetMouseButton() {
  return data_.mouse_button;
}

PP_Point PPB_InputEvent_Shared::GetMousePosition() {
  return data_.mouse_position;
}

int32_t PPB_InputEvent_Shared::GetMouseClickCount() {
  return data_.mouse_click_count;
}

PP_Point PPB_InputEvent_Shared::GetMouseMovement() {
  return data_.mouse_movement;
}

PP_FloatPoint PPB_InputEvent_Shared::GetWheelDelta() {
  return data_.wheel_delta;
}

PP_FloatPoint PPB_InputEvent_Shared::GetWheelTicks() {
  return data_.wheel_ticks;
}

PP_Bool PPB_InputEvent_Shared::GetWheelScrollByPage() {
  return PP_FromBool(data_.wheel_scroll_by_page);
}

uint32_t PPB_InputEvent_Shared::GetKeyCode() {
  return data_.key_code;
}

PP_Var PPB_InputEvent_Shared::GetCharacterText() {
  return StringVar::StringToPPVar(data_.character_text);
}

PP_Bool PPB_InputEvent_Shared::SetUsbKeyCode(uint32_t usb_key_code) {
  data_.usb_key_code = usb_key_code;
  return PP_TRUE;
}

uint32_t PPB_InputEvent_Shared::GetUsbKeyCode() {
  return data_.usb_key_code;
}

uint32_t PPB_InputEvent_Shared::GetIMESegmentNumber() {
  if (data_.composition_segment_offsets.empty())
    return 0;
  return data_.composition_segment_offsets.size() - 1;
}

uint32_t PPB_InputEvent_Shared::GetIMESegmentOffset(uint32_t index) {
  if (index >= data_.composition_segment_offsets.size())
    return 0;
  return data_.composition_segment_offsets[index];
}

int32_t PPB_InputEvent_Shared::GetIMETargetSegment() {
  return data_.composition_target_segment;
}

void PPB_InputEvent_Shared::GetIMESelection(uint32_t* start, uint32_t* end) {
  if (start)
    *start = data_.composition_selection_start;
  if (end)
    *end = data_.composition_selection_end;
}

//static
PP_Resource PPB_InputEvent_Shared::CreateIMEInputEvent(
    ResourceObjectType type,
    PP_Instance instance,
    PP_InputEvent_Type event_type,
    PP_TimeTicks time_stamp,
    struct PP_Var text,
    uint32_t segment_number,
    const uint32_t* segment_offsets,
    int32_t target_segment,
    uint32_t selection_start,
    uint32_t selection_end) {
  if (event_type != PP_INPUTEVENT_TYPE_IME_COMPOSITION_START &&
      event_type != PP_INPUTEVENT_TYPE_IME_COMPOSITION_UPDATE &&
      event_type != PP_INPUTEVENT_TYPE_IME_COMPOSITION_END &&
      event_type != PP_INPUTEVENT_TYPE_IME_TEXT)
    return 0;

  InputEventData data;
  data.event_type = event_type;
  data.event_time_stamp = time_stamp;
  if (text.type == PP_VARTYPE_STRING) {
    StringVar* text_str = StringVar::FromPPVar(text);
    if (!text_str)
      return 0;
    data.character_text = text_str->value();
  }
  data.composition_target_segment = target_segment;
  if (segment_number != 0) {
    data.composition_segment_offsets.assign(
        &segment_offsets[0], &segment_offsets[segment_number + 1]);
  }
  data.composition_selection_start = selection_start;
  data.composition_selection_end = selection_end;

  return (new PPB_InputEvent_Shared(type, instance, data))->GetReference();
}

//static
PP_Resource PPB_InputEvent_Shared::CreateKeyboardInputEvent(
    ResourceObjectType type,
    PP_Instance instance,
    PP_InputEvent_Type event_type,
    PP_TimeTicks time_stamp,
    uint32_t modifiers,
    uint32_t key_code,
    struct PP_Var character_text) {
  if (event_type != PP_INPUTEVENT_TYPE_RAWKEYDOWN &&
      event_type != PP_INPUTEVENT_TYPE_KEYDOWN &&
      event_type != PP_INPUTEVENT_TYPE_KEYUP &&
      event_type != PP_INPUTEVENT_TYPE_CHAR)
    return 0;

  InputEventData data;
  data.event_type = event_type;
  data.event_time_stamp = time_stamp;
  data.event_modifiers = modifiers;
  data.key_code = key_code;
  if (character_text.type == PP_VARTYPE_STRING) {
    StringVar* text_str = StringVar::FromPPVar(character_text);
    if (!text_str)
      return 0;
    data.character_text = text_str->value();
  }

  return (new PPB_InputEvent_Shared(type, instance, data))->GetReference();
}

//static
PP_Resource PPB_InputEvent_Shared::CreateMouseInputEvent(
    ResourceObjectType type,
    PP_Instance instance,
    PP_InputEvent_Type event_type,
    PP_TimeTicks time_stamp,
    uint32_t modifiers,
    PP_InputEvent_MouseButton mouse_button,
    const PP_Point* mouse_position,
    int32_t click_count,
    const PP_Point* mouse_movement) {
  if (event_type != PP_INPUTEVENT_TYPE_MOUSEDOWN &&
      event_type != PP_INPUTEVENT_TYPE_MOUSEUP &&
      event_type != PP_INPUTEVENT_TYPE_MOUSEMOVE &&
      event_type != PP_INPUTEVENT_TYPE_MOUSEENTER &&
      event_type != PP_INPUTEVENT_TYPE_MOUSELEAVE)
    return 0;

  InputEventData data;
  data.event_type = event_type;
  data.event_time_stamp = time_stamp;
  data.event_modifiers = modifiers;
  data.mouse_button = mouse_button;
  data.mouse_position = *mouse_position;
  data.mouse_click_count = click_count;
  data.mouse_movement = *mouse_movement;

  return (new PPB_InputEvent_Shared(type, instance, data))->GetReference();
}

//static
PP_Resource PPB_InputEvent_Shared::CreateWheelInputEvent(
    ResourceObjectType type,
    PP_Instance instance,
    PP_TimeTicks time_stamp,
    uint32_t modifiers,
    const PP_FloatPoint* wheel_delta,
    const PP_FloatPoint* wheel_ticks,
    PP_Bool scroll_by_page) {
  InputEventData data;
  data.event_type = PP_INPUTEVENT_TYPE_WHEEL;
  data.event_time_stamp = time_stamp;
  data.event_modifiers = modifiers;
  data.wheel_delta = *wheel_delta;
  data.wheel_ticks = *wheel_ticks;
  data.wheel_scroll_by_page = PP_ToBool(scroll_by_page);

  return (new PPB_InputEvent_Shared(type, instance, data))->GetReference();
}

}  // namespace ppapi
