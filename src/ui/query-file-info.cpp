// SPDX-License-Identifier: GPL-2.0-or-later
//
// Created by Michael Kowalski on 4/30/25.
//

#include "query-file-info.h"

#include <iostream>


namespace Inkscape::UI {

QueryFileInfo::QueryFileInfo(const std::string& path_to_test, std::function<void (Glib::RefPtr<Gio::FileInfo>)> on_result):
    _on_result(on_result) {

    _file = Gio::File::create_for_path(path_to_test);
    _operation = Gio::Cancellable::create();
    _file->query_info_async([this](auto& r) { results(r); }, _operation);
}

QueryFileInfo::~QueryFileInfo() {
    _operation->cancel();
}

void QueryFileInfo::results(Glib::RefPtr<Gio::AsyncResult>& result) const {
    try {
        auto info = _file->query_info_finish(result);
        _on_result(info);
    }
    catch (Glib::Error& ex) {
        if (ex.code() == 1) {
            // path points to nonexistent object
            _on_result({});
            return;
        }
        std::cerr << "Async file query error: " << ex.what() << ", " << ex.code() << std::endl;
    }
}

} // namespace
