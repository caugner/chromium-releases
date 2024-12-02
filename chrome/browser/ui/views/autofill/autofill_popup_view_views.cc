// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "autofill_popup_view_views.h"

#include "chrome/browser/ui/views/autofill/autofill_external_delegate_views.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_view.h"
#include "grit/ui_resources.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebAutofillClient.h"
#include "ui/base/keycodes/keyboard_codes.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/rect.h"
#include "ui/views/border.h"
#include "ui/views/widget/widget.h"

using WebKit::WebAutofillClient;

namespace {
const SkColor kBorderColor = SkColorSetARGB(0xFF, 0xC7, 0xCA, 0xCE);
const SkColor kHoveredBackgroundColor = SkColorSetARGB(0xFF, 0xCD, 0xCD, 0xCD);
const SkColor kLabelTextColor = SkColorSetARGB(0xFF, 0x7F, 0x7F, 0x7F);
const SkColor kPopupBackground = SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);
const SkColor kValueTextColor = SkColorSetARGB(0xFF, 0x00, 0x00, 0x00);

}  // namespace

AutofillPopupViewViews::AutofillPopupViewViews(
    content::WebContents* web_contents,
    AutofillExternalDelegateViews* external_delegate)
    : AutofillPopupView(web_contents, external_delegate),
      external_delegate_(external_delegate),
      web_contents_(web_contents) {
}

AutofillPopupViewViews::~AutofillPopupViewViews() {
  external_delegate_->InvalidateView();
}

void AutofillPopupViewViews::OnPaint(gfx::Canvas* canvas) {
  canvas->DrawColor(kPopupBackground);
  OnPaintBorder(canvas);

  for (size_t i = 0; i < autofill_values().size(); ++i) {
    gfx::Rect line_rect = GetRectForRow(i, width());

    if (autofill_unique_ids()[i] == WebAutofillClient::MenuItemIDSeparator)
      canvas->DrawRect(line_rect, kLabelTextColor);
    else
      DrawAutofillEntry(canvas, i, line_rect);
  }
}

bool AutofillPopupViewViews::HandleKeyPressEvent(ui::KeyEvent* event) {
  switch (event->key_code()) {
    case ui::VKEY_UP:
      SelectPreviousLine();
      return true;
    case ui::VKEY_DOWN:
      SelectNextLine();
      return true;
    case ui::VKEY_PRIOR:
      SetSelectedLine(0);
      return true;
    case ui::VKEY_NEXT:
      SetSelectedLine(autofill_values().size() - 1);
      return true;
    case ui::VKEY_ESCAPE:
      Hide();
      return true;
    case ui::VKEY_DELETE:
      return event->IsShiftDown() && RemoveSelectedLine();
    case ui::VKEY_RETURN:
      return AcceptSelectedLine();
    default:
      return false;
  }
}

void AutofillPopupViewViews::ShowInternal() {
  if (!GetWidget()) {
    // The widget is destroyed by the corresponding NativeWidget, so we use
    // a weak pointer to hold the reference and don't have to worry about
    // deletion.
    views::Widget* widget = new views::Widget;
    views::Widget::InitParams params(views::Widget::InitParams::TYPE_POPUP);
    params.delegate = this;
    params.can_activate = false;
    params.transparent = true;
    params.parent = web_contents_->GetView()->GetTopLevelNativeWindow();
    widget->Init(params);
    widget->SetContentsView(this);
    widget->Show();

    gfx::Rect client_area;
    web_contents_->GetContainerBounds(&client_area);
    widget->SetBounds(client_area);
  }

  set_border(views::Border::CreateSolidBorder(kBorderThickness, kBorderColor));

  ResizePopup();

  web_contents_->GetRenderViewHost()->AddKeyboardListener(this);
}

void AutofillPopupViewViews::HideInternal() {
  if (GetWidget())
    GetWidget()->Close();
  web_contents_->GetRenderViewHost()->RemoveKeyboardListener(this);
}

void AutofillPopupViewViews::InvalidateRow(size_t row) {
  SchedulePaintInRect(GetRectForRow(row, width()));
}

void AutofillPopupViewViews::ResizePopup() {
  gfx::Rect popup_bounds = element_bounds();
  popup_bounds.set_y(popup_bounds.y() + popup_bounds.height());

  popup_bounds.set_width(GetPopupRequiredWidth());
  popup_bounds.set_height(GetPopupRequiredHeight());

  SetBoundsRect(popup_bounds);
}

void AutofillPopupViewViews::DrawAutofillEntry(gfx::Canvas* canvas,
                                               int index,
                                               const gfx::Rect& entry_rect) {
    // TODO(csharp): support RTL

  if (selected_line() == index)
    canvas->FillRect(entry_rect, kHoveredBackgroundColor);

  canvas->DrawStringInt(
      autofill_values()[index],
      value_font(),
      kValueTextColor,
      kEndPadding,
      entry_rect.y(),
      canvas->GetStringWidth(autofill_values()[index], value_font()),
      entry_rect.height(),
      gfx::Canvas::TEXT_ALIGN_CENTER);

  // Use this to figure out where all the other Autofill items should be placed.
  int x_align_left = entry_rect.width() - kEndPadding;

  // Draw the delete icon, if one is needed.
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  int row_height = GetRowHeightFromId(autofill_unique_ids()[index]);
  if (CanDelete(autofill_unique_ids()[index])) {
    x_align_left -= kDeleteIconWidth;

    // TODO(csharp): Create a custom resource for the delete icon.
    // http://www.crbug.com/131801
    canvas->DrawImageInt(
        *rb.GetImageSkiaNamed(IDR_CLOSE_BAR),
        x_align_left,
        entry_rect.y() + ((row_height - kDeleteIconHeight) / 2));

    x_align_left -= kIconPadding;
  }

  // Draw the Autofill icon, if one exists
  if (!autofill_icons()[index].empty()) {
    int icon = GetIconResourceID(autofill_icons()[index]);
    DCHECK_NE(-1, icon);
    int icon_y = entry_rect.y() + ((row_height - kAutofillIconHeight) / 2);

    x_align_left -= kAutofillIconWidth;

    canvas->DrawImageInt(*rb.GetImageSkiaNamed(icon), x_align_left, icon_y);

    x_align_left -= kIconPadding;
  }

  // Draw the label text.
  x_align_left -= canvas->GetStringWidth(autofill_labels()[index],
                                         label_font());

  canvas->DrawStringInt(
      autofill_labels()[index],
      label_font(),
      kLabelTextColor,
      x_align_left + kEndPadding,
      entry_rect.y(),
      canvas->GetStringWidth(autofill_labels()[index], label_font()),
      entry_rect.height(),
      gfx::Canvas::TEXT_ALIGN_CENTER);
}
