/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import QtQuick 2.0
import QtQuick.Layouts 1.14
import Mozilla.VPN 1.0
import components 0.1

VPNIconButton {
    id: root

    property var _screen
    property alias _source: image.source
    property bool _hasNotification: true

    accessibleName: ""

    checked: VPNNavigator.screen === _screen

    onClicked: {
        VPNNavigator.requestScreen(_screen, VPNNavigator.screen === _screen);
    }

    onCheckedChanged: if (checked) root.forceActiveFocus();


    Layout.alignment: Qt.AlignHCenter
    Layout.preferredWidth: VPNTheme.theme.navBarTopMargin
    Layout.preferredHeight: VPNTheme.theme.navBarTopMargin

    backgroundRadius: height / 2
    buttonColorScheme: VPNTheme.theme.iconButtonDarkBackground
    uiStatesVisible: !checked

    Rectangle {
        id: circleBackground
        anchors.fill: parent
        visible: checked
        radius: parent.height / 2
        opacity: .2
        color: VPNTheme.theme.white
    }

    Image {
        id: image
        anchors.centerIn: parent
        sourceSize.height: VPNTheme.theme.iconSize * 2
        sourceSize.width: VPNTheme.theme.iconSize * 2
    }
}