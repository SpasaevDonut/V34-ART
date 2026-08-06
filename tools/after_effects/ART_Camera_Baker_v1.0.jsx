#target aftereffects

/*
    ART Camera Baker
    Version 1.0

    Select one time-remapped precomp layer in the edit/master composition.
    The script reads the selected layer's Time Remap property, samples the
    original ART camera and every null layer inside the source precomp, and
    writes ordinary retimed keyframes into the edit/master composition.
*/

(function artCameraBaker(thisObj) {
    var SCRIPT_NAME = "ART Camera Baker";
    var SCRIPT_VERSION = "v1.0";

    var MATCH = {
        CAMERA_LAYER: "ADBE Camera Layer",
        TRANSFORM: "ADBE Transform Group",
        ANCHOR_POINT: "ADBE Anchor Point",
        POINT_OF_INTEREST: "ADBE Point of Interest",
        POSITION: "ADBE Position",
        SCALE: "ADBE Scale",
        ORIENTATION: "ADBE Orientation",
        X_ROTATION: "ADBE Rotate X",
        Y_ROTATION: "ADBE Rotate Y",
        Z_ROTATION: "ADBE Rotate Z",
        OPACITY: "ADBE Opacity",
        SKEW: "ADBE Skew",
        SKEW_AXIS: "ADBE Skew Axis",
        CAMERA_OPTIONS: "ADBE Camera Options Group",
        ZOOM: "ADBE Camera Zoom",
        DEPTH_OF_FIELD: "ADBE Camera Depth of Field",
        FOCUS_DISTANCE: "ADBE Camera Focus Distance",
        APERTURE: "ADBE Camera Aperture",
        BLUR_LEVEL: "ADBE Camera Blur Level",
        TIME_REMAP: "ADBE Time Remapping"
    };

    function fail(message) {
        alert(SCRIPT_NAME + " " + SCRIPT_VERSION + "\n\n" + message);
        return null;
    }

    function isComp(item) {
        return item && (item instanceof CompItem);
    }

    function isCameraLayer(layer) {
        if (!layer) {
            return false;
        }

        try {
            if (layer.matchName === MATCH.CAMERA_LAYER) {
                return true;
            }
        } catch (ignoreMatchName) {}

        try {
            return layer instanceof CameraLayer;
        } catch (ignoreCameraClass) {
            return false;
        }
    }

    function getPropertyByMatchName(group, matchName) {
        if (!group) {
            return null;
        }

        try {
            var direct = group.property(matchName);
            if (direct) {
                return direct;
            }
        } catch (ignoreDirect) {}

        var count = 0;
        try {
            count = group.numProperties || 0;
        } catch (ignoreCount) {
            count = 0;
        }

        for (var i = 1; i <= count; i++) {
            var prop = null;
            try {
                prop = group.property(i);
            } catch (ignoreProperty) {}

            if (prop) {
                try {
                    if (prop.matchName === matchName) {
                        return prop;
                    }
                } catch (ignorePropMatchName) {}
            }
        }

        return null;
    }

    function collectCameras(comp) {
        var result = [];
        for (var i = 1; i <= comp.numLayers; i++) {
            var layer = comp.layer(i);
            if (isCameraLayer(layer)) {
                result.push(layer);
            }
        }
        return result;
    }

    function isNullLayer(layer) {
        if (!layer) {
            return false;
        }

        try {
            return layer.nullLayer === true;
        } catch (ignoreNullLayer) {
            return false;
        }
    }

    function collectNulls(comp) {
        var result = [];
        for (var i = 1; i <= comp.numLayers; i++) {
            var layer = comp.layer(i);
            if (isNullLayer(layer)) {
                result.push(layer);
            }
        }
        return result;
    }

    function nearlyEqual(a, b, epsilon) {
        return Math.abs(a - b) <= epsilon;
    }

    function arraysNearlyEqual(a, b, epsilon) {
        if (!a || !b || a.length === undefined || b.length === undefined || a.length !== b.length) {
            return false;
        }

        for (var i = 0; i < a.length; i++) {
            if (!nearlyEqual(a[i], b[i], epsilon)) {
                return false;
            }
        }
        return true;
    }

    function shotLayerHasDefaultTransform(shotLayer, sourceComp, targetComp) {
        try {
            if (shotLayer.threeDLayer) {
                return false;
            }
            if (shotLayer.parent !== null) {
                return false;
            }

            var transform = shotLayer.property(MATCH.TRANSFORM);
            var anchor = getPropertyByMatchName(transform, "ADBE Anchor Point");
            var position = getPropertyByMatchName(transform, MATCH.POSITION);
            var scale = getPropertyByMatchName(transform, "ADBE Scale");
            var rotation = getPropertyByMatchName(transform, MATCH.Z_ROTATION);

            var expectedAnchor = [sourceComp.width / 2, sourceComp.height / 2];
            var expectedPosition = [targetComp.width / 2, targetComp.height / 2];

            if (anchor && !arraysNearlyEqual(anchor.value, expectedAnchor, 0.01)) {
                return false;
            }
            if (position && !arraysNearlyEqual(position.value, expectedPosition, 0.01)) {
                return false;
            }
            if (scale && !arraysNearlyEqual(scale.value, [100, 100], 0.01)) {
                return false;
            }
            if (rotation && !nearlyEqual(rotation.value, 0, 0.001)) {
                return false;
            }
        } catch (ignoreTransformCheck) {
            return false;
        }

        return true;
    }

    function uniqueLayerName(comp, baseName) {
        var candidate = baseName;
        var suffix = 2;
        var exists = true;

        while (exists) {
            exists = false;
            for (var i = 1; i <= comp.numLayers; i++) {
                if (comp.layer(i).name === candidate) {
                    exists = true;
                    candidate = baseName + " " + suffix;
                    suffix++;
                    break;
                }
            }
        }

        return candidate;
    }

    function addTarget(targets, sourceProperty, destinationProperty, displayName, isDiscrete) {
        if (!sourceProperty || !destinationProperty) {
            return;
        }

        targets.push({
            source: sourceProperty,
            destination: destinationProperty,
            displayName: displayName,
            isDiscrete: !!isDiscrete,
            values: []
        });
    }

    function setLinearInterpolation(property) {
        try {
            for (var i = 1; i <= property.numKeys; i++) {
                property.setInterpolationTypeAtKey(
                    i,
                    KeyframeInterpolationType.LINEAR,
                    KeyframeInterpolationType.LINEAR
                );
            }
        } catch (ignoreInterpolation) {}
    }

    function setHoldInterpolation(property) {
        try {
            for (var i = 1; i <= property.numKeys; i++) {
                property.setInterpolationTypeAtKey(
                    i,
                    KeyframeInterpolationType.HOLD,
                    KeyframeInterpolationType.HOLD
                );
            }
        } catch (ignoreInterpolation) {}
    }

    function buildDialog(targetComp, shotLayer, sourceComp, cameras, nulls) {
        var dialog = new Window("dialog", SCRIPT_NAME + " " + SCRIPT_VERSION);
        dialog.orientation = "column";
        dialog.alignChildren = ["fill", "top"];
        dialog.spacing = 10;
        dialog.margins = 14;

        var info = dialog.add("panel", undefined, "Selected shot");
        info.orientation = "column";
        info.alignChildren = ["left", "top"];
        info.margins = 10;
        info.add("statictext", undefined, "Layer: " + shotLayer.name);
        info.add("statictext", undefined, "Source comp: " + sourceComp.name);
        info.add("statictext", undefined, "Source cameras: " + cameras.length);
        info.add("statictext", undefined, "Source nulls: " + nulls.length);
        info.add("statictext", undefined, "Source FPS: " + sourceComp.frameRate.toFixed(3));
        info.add("statictext", undefined, "Edit FPS: " + targetComp.frameRate.toFixed(3));
        info.add("statictext", undefined, "Source/edit ratio: " + (sourceComp.frameRate / targetComp.frameRate).toFixed(3) + " samples per edit frame");
        info.add("statictext", undefined, "Time Remap: " + (shotLayer.timeRemapEnabled ? "enabled" : "not enabled"));

        var cameraGroup = dialog.add("group");
        cameraGroup.orientation = "row";
        cameraGroup.alignChildren = ["left", "center"];
        cameraGroup.add("statictext", undefined, "Source camera:");
        var cameraDropdown = cameraGroup.add("dropdownlist", undefined, []);
        cameraDropdown.preferredSize.width = 280;
        for (var i = 0; i < cameras.length; i++) {
            cameraDropdown.add("item", cameras[i].name + "  [layer " + cameras[i].index + "]");
        }
        cameraDropdown.selection = 0;

        var rangePanel = dialog.add("panel", undefined, "Bake range");
        rangePanel.orientation = "column";
        rangePanel.alignChildren = ["left", "top"];
        rangePanel.margins = 10;
        var rangeShot = rangePanel.add("radiobutton", undefined, "Selected shot layer in/out points");
        var rangeWork = rangePanel.add("radiobutton", undefined, "Composition work area");
        var rangeFull = rangePanel.add("radiobutton", undefined, "Full composition");
        rangeShot.value = true;

        var samplingPanel = dialog.add("panel", undefined, "Sampling");
        samplingPanel.orientation = "column";
        samplingPanel.alignChildren = ["left", "top"];
        samplingPanel.margins = 10;

        var sampleSource = samplingPanel.add(
            "radiobutton",
            undefined,
            "Match source comp FPS: " + sourceComp.frameRate.toFixed(3) + " fps (recommended)"
        );
        var sampleEdit = samplingPanel.add(
            "radiobutton",
            undefined,
            "Edit comp frames only: " + targetComp.frameRate.toFixed(3) + " fps"
        );
        var customGroup = samplingPanel.add("group");
        customGroup.orientation = "row";
        customGroup.alignChildren = ["left", "center"];
        var sampleCustom = customGroup.add("radiobutton", undefined, "Custom bake FPS:");
        var customFpsField = customGroup.add("edittext", undefined, sourceComp.frameRate.toFixed(3));
        customFpsField.characters = 8;
        customGroup.add("statictext", undefined, "fps");
        sampleSource.value = true;

        var sampleHint = samplingPanel.add(
            "statictext",
            undefined,
            "At " + sourceComp.frameRate.toFixed(3) + " → " + targetComp.frameRate.toFixed(3) +
            " fps, source-FPS mode creates about " +
            (sourceComp.frameRate / targetComp.frameRate).toFixed(3) +
            " subframe samples per edit frame.",
            { multiline: true }
        );
        sampleHint.maximumSize.height = 42;

        var optionsPanel = dialog.add("panel", undefined, "Properties and layers");
        optionsPanel.orientation = "column";
        optionsPanel.alignChildren = ["left", "top"];
        optionsPanel.margins = 10;
        var includeDof = optionsPanel.add("checkbox", undefined, "Bake depth of field properties");
        includeDof.value = true;
        var includeNulls = optionsPanel.add(
            "checkbox",
            undefined,
            "Bake all null layers from source comp (" + nulls.length + ")"
        );
        includeNulls.value = nulls.length > 0;
        includeNulls.enabled = nulls.length > 0;

        var warning = dialog.add(
            "statictext",
            undefined,
            "The camera and nulls are sampled from the source comp through the selected layer's Time Remap.\n" +
            "Parenting between the selected camera and source nulls is recreated when possible.",
            { multiline: true }
        );
        warning.maximumSize.height = 46;

        var buttons = dialog.add("group");
        buttons.alignment = "right";
        var bakeButton = buttons.add("button", undefined, "Bake", { name: "ok" });
        var cancelButton = buttons.add("button", undefined, "Cancel", { name: "cancel" });

        var accepted = false;
        var selectedCameraIndex = 0;
        var selectedSampleRate = sourceComp.frameRate;
        var selectedRangeMode = "shot";
        var selectedIncludeDof = true;
        var selectedIncludeNulls = nulls.length > 0;

        bakeButton.onClick = function () {
            if (!cameraDropdown.selection) {
                alert("Choose a source camera.");
                return;
            }

            selectedCameraIndex = cameraDropdown.selection.index;
            if (sampleEdit.value) {
                selectedSampleRate = targetComp.frameRate;
            } else if (sampleCustom.value) {
                selectedSampleRate = parseFloat(customFpsField.text);
                if (!isFinite(selectedSampleRate) || selectedSampleRate <= 0) {
                    alert("Enter a valid custom bake FPS greater than zero.");
                    return;
                }
            } else {
                selectedSampleRate = sourceComp.frameRate;
            }

            selectedRangeMode = rangeWork.value ? "work" : (rangeFull.value ? "full" : "shot");
            selectedIncludeDof = includeDof.value;
            selectedIncludeNulls = includeNulls.value && nulls.length > 0;
            accepted = true;
            dialog.close(1);
        };

        cancelButton.onClick = function () {
            dialog.close(0);
        };

        dialog.show();
        if (!accepted) {
            return null;
        }

        return {
            camera: cameras[selectedCameraIndex],
            rangeMode: selectedRangeMode,
            sampleRate: selectedSampleRate,
            includeDof: selectedIncludeDof,
            includeNulls: selectedIncludeNulls
        };
    }

    function getBakeRange(comp, shotLayer, rangeMode) {
        var start;
        var end;

        if (rangeMode === "work") {
            start = comp.workAreaStart;
            end = comp.workAreaStart + comp.workAreaDuration;
        } else if (rangeMode === "full") {
            start = 0;
            end = comp.duration;
        } else {
            start = shotLayer.inPoint;
            end = shotLayer.outPoint;
        }

        start = Math.max(0, start);
        end = Math.min(comp.duration, end);

        if (end <= start) {
            return null;
        }

        return { start: start, end: end };
    }

    function buildSampleTimes(start, end, sampleRate) {
        if (!isFinite(sampleRate) || sampleRate <= 0) {
            throw new Error("Invalid bake sampling rate: " + sampleRate);
        }

        var step = 1.0 / sampleRate;
        var epsilon = step * 0.01;
        var times = [];
        var guard = 0;
        var t = start;

        while (t < end - epsilon) {
            times.push(t);
            guard++;
            if (guard > 1000000) {
                throw new Error("The bake range contains too many samples.");
            }
            t = start + guard * step;
        }

        if (times.length === 0) {
            times.push(start);
        }

        return times;
    }

    function buildProgressWindow(totalSamples) {
        var win = new Window("palette", SCRIPT_NAME + " " + SCRIPT_VERSION);
        win.orientation = "column";
        win.alignChildren = ["fill", "top"];
        win.margins = 12;

        var status = win.add("statictext", undefined, "Preparing camera...");
        status.preferredSize.width = 420;

        var progress = win.add("progressbar", undefined, 0, Math.max(1, totalSamples));
        progress.preferredSize = [420, 18];

        var cancelButton = win.add("button", undefined, "Cancel");
        var state = { cancelled: false };
        cancelButton.onClick = function () {
            state.cancelled = true;
            status.text = "Cancelling...";
        };

        win.show();
        win.update();

        return {
            window: win,
            status: status,
            progress: progress,
            state: state
        };
    }

    function getTimeRemapProperty(shotLayer) {
        var prop = null;

        try {
            prop = shotLayer.property(MATCH.TIME_REMAP);
        } catch (ignoreMatchName) {}

        if (!prop) {
            try {
                prop = shotLayer.timeRemap;
            } catch (ignoreTimeRemapShortcut) {}
        }

        return prop;
    }

    function getSourceTimeAt(shotLayer, timeRemapProperty, compTime) {
        if (shotLayer.timeRemapEnabled && timeRemapProperty) {
            return timeRemapProperty.valueAtTime(compTime, false);
        }

        var stretch = shotLayer.stretch;
        if (stretch === 0) {
            stretch = 100;
        }

        return (compTime - shotLayer.startTime) * (100 / stretch);
    }

    function copySampleValue(value) {
        if (value !== null && value !== undefined && value.length !== undefined && typeof value !== "string") {
            var copied = [];
            for (var i = 0; i < value.length; i++) {
                copied.push(value[i]);
            }
            return copied;
        }
        return value;
    }

    function copyLayerMetadata(sourceLayer, destinationLayer, sourceComp, shotLayer) {
        try {
            if (sourceLayer.label !== undefined) {
                destinationLayer.label = sourceLayer.label;
            }
        } catch (ignoreLabel) {}

        try {
            destinationLayer.shy = sourceLayer.shy;
        } catch (ignoreShy) {}

        try {
            destinationLayer.enabled = sourceLayer.enabled;
        } catch (ignoreEnabled) {}

        try {
            destinationLayer.motionBlur = sourceLayer.motionBlur;
        } catch (ignoreMotionBlur) {}

        try {
            destinationLayer.guideLayer = sourceLayer.guideLayer;
        } catch (ignoreGuideLayer) {}

        try {
            destinationLayer.autoOrient = sourceLayer.autoOrient;
        } catch (ignoreAutoOrient) {}

        try {
            destinationLayer.comment = "Baked by " + SCRIPT_NAME + " " + SCRIPT_VERSION +
                " from " + sourceComp.name + " / " + sourceLayer.name +
                " through shot layer " + shotLayer.name + ".";
        } catch (ignoreComment) {}
    }

    function addCameraTargets(targets, sourceCamera, destinationCamera, includeDof) {
        var sourceTransform = sourceCamera.property(MATCH.TRANSFORM);
        var destinationTransform = destinationCamera.property(MATCH.TRANSFORM);
        var sourceOptions = sourceCamera.property(MATCH.CAMERA_OPTIONS);
        var destinationOptions = destinationCamera.property(MATCH.CAMERA_OPTIONS);

        if (!sourceTransform || !destinationTransform) {
            throw new Error("Could not access the camera Transform group.");
        }

        var usePointOfInterest = false;
        try {
            usePointOfInterest = sourceCamera.autoOrient !== AutoOrientType.NO_AUTO_ORIENT;
        } catch (ignoreOrientType) {
            usePointOfInterest = true;
        }

        if (usePointOfInterest) {
            addTarget(
                targets,
                getPropertyByMatchName(sourceTransform, MATCH.POINT_OF_INTEREST),
                getPropertyByMatchName(destinationTransform, MATCH.POINT_OF_INTEREST),
                sourceCamera.name + " / Point of Interest",
                false
            );
        }

        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.POSITION),
            getPropertyByMatchName(destinationTransform, MATCH.POSITION),
            sourceCamera.name + " / Position", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.ORIENTATION),
            getPropertyByMatchName(destinationTransform, MATCH.ORIENTATION),
            sourceCamera.name + " / Orientation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.X_ROTATION),
            getPropertyByMatchName(destinationTransform, MATCH.X_ROTATION),
            sourceCamera.name + " / X Rotation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.Y_ROTATION),
            getPropertyByMatchName(destinationTransform, MATCH.Y_ROTATION),
            sourceCamera.name + " / Y Rotation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.Z_ROTATION),
            getPropertyByMatchName(destinationTransform, MATCH.Z_ROTATION),
            sourceCamera.name + " / Z Rotation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceOptions, MATCH.ZOOM),
            getPropertyByMatchName(destinationOptions, MATCH.ZOOM),
            sourceCamera.name + " / Zoom", false);

        if (includeDof) {
            addTarget(targets,
                getPropertyByMatchName(sourceOptions, MATCH.DEPTH_OF_FIELD),
                getPropertyByMatchName(destinationOptions, MATCH.DEPTH_OF_FIELD),
                sourceCamera.name + " / Depth of Field", true);
            addTarget(targets,
                getPropertyByMatchName(sourceOptions, MATCH.FOCUS_DISTANCE),
                getPropertyByMatchName(destinationOptions, MATCH.FOCUS_DISTANCE),
                sourceCamera.name + " / Focus Distance", false);
            addTarget(targets,
                getPropertyByMatchName(sourceOptions, MATCH.APERTURE),
                getPropertyByMatchName(destinationOptions, MATCH.APERTURE),
                sourceCamera.name + " / Aperture", false);
            addTarget(targets,
                getPropertyByMatchName(sourceOptions, MATCH.BLUR_LEVEL),
                getPropertyByMatchName(destinationOptions, MATCH.BLUR_LEVEL),
                sourceCamera.name + " / Blur Level", false);
        }
    }

    function addNullTargets(targets, sourceNull, destinationNull) {
        var sourceTransform = sourceNull.property(MATCH.TRANSFORM);
        var destinationTransform = destinationNull.property(MATCH.TRANSFORM);

        if (!sourceTransform || !destinationTransform) {
            throw new Error("Could not access Transform for null: " + sourceNull.name);
        }

        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.ANCHOR_POINT),
            getPropertyByMatchName(destinationTransform, MATCH.ANCHOR_POINT),
            sourceNull.name + " / Anchor Point", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.POSITION),
            getPropertyByMatchName(destinationTransform, MATCH.POSITION),
            sourceNull.name + " / Position", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.SCALE),
            getPropertyByMatchName(destinationTransform, MATCH.SCALE),
            sourceNull.name + " / Scale", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.ORIENTATION),
            getPropertyByMatchName(destinationTransform, MATCH.ORIENTATION),
            sourceNull.name + " / Orientation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.X_ROTATION),
            getPropertyByMatchName(destinationTransform, MATCH.X_ROTATION),
            sourceNull.name + " / X Rotation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.Y_ROTATION),
            getPropertyByMatchName(destinationTransform, MATCH.Y_ROTATION),
            sourceNull.name + " / Y Rotation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.Z_ROTATION),
            getPropertyByMatchName(destinationTransform, MATCH.Z_ROTATION),
            sourceNull.name + " / Rotation", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.SKEW),
            getPropertyByMatchName(destinationTransform, MATCH.SKEW),
            sourceNull.name + " / Skew", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.SKEW_AXIS),
            getPropertyByMatchName(destinationTransform, MATCH.SKEW_AXIS),
            sourceNull.name + " / Skew Axis", false);
        addTarget(targets,
            getPropertyByMatchName(sourceTransform, MATCH.OPACITY),
            getPropertyByMatchName(destinationTransform, MATCH.OPACITY),
            sourceNull.name + " / Opacity", false);
    }

    function createDestinationNull(comp, sourceNull, range, sourceComp, shotLayer) {
        var duration = Math.max(comp.frameDuration, range.end - range.start);
        var destinationNull = comp.layers.addNull(duration);
        destinationNull.name = uniqueLayerName(comp, sourceNull.name);
        destinationNull.inPoint = range.start;
        destinationNull.outPoint = range.end;

        try {
            destinationNull.threeDLayer = sourceNull.threeDLayer;
        } catch (ignoreThreeD) {}

        copyLayerMetadata(sourceNull, destinationNull, sourceComp, shotLayer);
        return destinationNull;
    }

    function setParentWithoutChangingLocalValues(destinationLayer, destinationParent) {
        if (!destinationLayer || !destinationParent) {
            return;
        }

        try {
            if (typeof destinationLayer.setParentWithJump === "function") {
                destinationLayer.setParentWithJump(destinationParent);
                return;
            }
        } catch (ignoreSetParentWithJump) {}

        try {
            destinationLayer.parent = destinationParent;
        } catch (ignoreParentAssignment) {}
    }

    function removeCreatedLayers(createdLayers) {
        for (var i = createdLayers.length - 1; i >= 0; i--) {
            try {
                if (createdLayers[i]) {
                    createdLayers[i].remove();
                }
            } catch (ignoreCleanup) {}
        }
    }

    function bakeCameraAndNulls(comp, shotLayer, sourceComp, sourceCamera, sourceNulls, settings, stageState) {
        stageState.text = "Calculating bake range";
        var range = getBakeRange(comp, shotLayer, settings.rangeMode);
        if (!range) {
            throw new Error("The selected bake range is empty.");
        }

        stageState.text = "Building sample times";
        var times = buildSampleTimes(range.start, range.end, settings.sampleRate);
        var nullsToBake = settings.includeNulls ? sourceNulls : [];
        var roughPropertyCount = (settings.includeDof ? 11 : 7) + (nullsToBake.length * 10);
        var estimatedKeys = times.length * roughPropertyCount;
        if (estimatedKeys > 250000) {
            var continueLargeBake = confirm(
                SCRIPT_NAME + "\n\n" +
                "This operation may create approximately " + estimatedKeys + " keyframes across " +
                (1 + nullsToBake.length) + " layers.\n" +
                "Continue?"
            );
            if (!continueLargeBake) {
                return null;
            }
        }

        var createdLayers = [];
        var layerEntries = [];
        var destinationBySourceIndex = {};
        var newCamera = null;
        var progressUi = null;
        var completed = false;
        var currentTargetName = "";
        var skippedParentLinks = 0;

        try {
            stageState.text = "Creating destination camera";
            var newName = uniqueLayerName(comp, sourceCamera.name + " [Retimed]");
            newCamera = comp.layers.addCamera(newName, [comp.width / 2, comp.height / 2]);
            newCamera.inPoint = range.start;
            newCamera.outPoint = range.end;
            copyLayerMetadata(sourceCamera, newCamera, sourceComp, shotLayer);
            createdLayers.push(newCamera);
            layerEntries.push({
                source: sourceCamera,
                destination: newCamera,
                kind: "camera",
                restoreLocked: false
            });
            destinationBySourceIndex[sourceCamera.index] = newCamera;

            stageState.text = "Creating destination nulls";
            for (var nullIndex = 0; nullIndex < nullsToBake.length; nullIndex++) {
                var sourceNull = nullsToBake[nullIndex];
                var destinationNull = createDestinationNull(comp, sourceNull, range, sourceComp, shotLayer);
                createdLayers.push(destinationNull);
                layerEntries.push({
                    source: sourceNull,
                    destination: destinationNull,
                    kind: "null",
                    restoreLocked: false
                });
                destinationBySourceIndex[sourceNull.index] = destinationNull;
            }

            stageState.text = "Recreating parenting";
            for (var entryIndex = 0; entryIndex < layerEntries.length; entryIndex++) {
                var entry = layerEntries[entryIndex];
                try {
                    entry.restoreLocked = entry.source.locked === true;
                } catch (ignoreLockedRead) {
                    entry.restoreLocked = false;
                }

                var sourceParent = null;
                try {
                    sourceParent = entry.source.parent;
                } catch (ignoreParentRead) {}

                if (sourceParent) {
                    var destinationParent = destinationBySourceIndex[sourceParent.index];
                    if (destinationParent) {
                        setParentWithoutChangingLocalValues(entry.destination, destinationParent);
                    } else {
                        skippedParentLinks++;
                    }
                }
            }

            stageState.text = "Resolving transform properties";
            var targets = [];
            addCameraTargets(targets, sourceCamera, newCamera, settings.includeDof);

            for (var targetNullIndex = 0; targetNullIndex < layerEntries.length; targetNullIndex++) {
                var nullEntry = layerEntries[targetNullIndex];
                if (nullEntry.kind === "null") {
                    addNullTargets(targets, nullEntry.source, nullEntry.destination);
                }
            }

            if (targets.length === 0) {
                throw new Error("No compatible camera or null properties were found.");
            }

            stageState.text = "Resolving Time Remap property";
            var timeRemapProperty = getTimeRemapProperty(shotLayer);
            if (shotLayer.timeRemapEnabled && !timeRemapProperty) {
                throw new Error("Time Remapping is enabled, but the Time Remap property could not be accessed.");
            }

            progressUi = buildProgressWindow(times.length);
            stageState.text = "Sampling camera and nulls";
            for (var timeIndex = 0; timeIndex < times.length; timeIndex++) {
                if (progressUi.state.cancelled) {
                    throw new Error("__USER_CANCELLED__");
                }

                var compTime = times[timeIndex];
                var sourceTime = getSourceTimeAt(shotLayer, timeRemapProperty, compTime);

                for (var targetIndex = 0; targetIndex < targets.length; targetIndex++) {
                    currentTargetName = targets[targetIndex].displayName;
                    var sampledValue = targets[targetIndex].source.valueAtTime(sourceTime, false);
                    targets[targetIndex].values.push(copySampleValue(sampledValue));
                }

                if (timeIndex % 5 === 0 || timeIndex === times.length - 1) {
                    progressUi.progress.value = timeIndex + 1;
                    progressUi.status.text =
                        "Sampling " + (timeIndex + 1) + " / " + times.length +
                        " | source " + sourceTime.toFixed(3) + "s";
                    progressUi.window.update();
                }
            }

            stageState.text = "Writing keyframes";
            progressUi.status.text = "Writing keyframes...";
            progressUi.window.update();

            for (var writeIndex = 0; writeIndex < targets.length; writeIndex++) {
                var target = targets[writeIndex];
                currentTargetName = target.displayName;
                progressUi.status.text = "Writing " + currentTargetName + "...";
                progressUi.window.update();

                target.destination.setValuesAtTimes(times, target.values);
                if (target.isDiscrete) {
                    setHoldInterpolation(target.destination);
                } else {
                    setLinearInterpolation(target.destination);
                }
            }

            for (var lockIndex = 0; lockIndex < layerEntries.length; lockIndex++) {
                if (layerEntries[lockIndex].restoreLocked) {
                    try {
                        layerEntries[lockIndex].destination.locked = true;
                    } catch (ignoreLockedWrite) {}
                }
            }

            completed = true;
        } catch (bakeError) {
            if (bakeError && bakeError.message === "__USER_CANCELLED__") {
                throw bakeError;
            }

            var detailedMessage = "Failure while processing " + (currentTargetName || "camera/null layers") + ".";
            if (bakeError && bakeError.message) {
                detailedMessage += "\n" + bakeError.message;
            } else {
                detailedMessage += "\n" + bakeError;
            }
            throw new Error(detailedMessage);
        } finally {
            if (progressUi) {
                try {
                    progressUi.window.close();
                } catch (ignoreProgressClose) {}
            }

            if (!completed) {
                removeCreatedLayers(createdLayers);
            }
        }

        stageState.text = "Selecting baked layers";
        for (var layerIndex = 1; layerIndex <= comp.numLayers; layerIndex++) {
            try {
                comp.layer(layerIndex).selected = false;
            } catch (ignoreSelection) {}
        }
        for (var selectIndex = 0; selectIndex < createdLayers.length; selectIndex++) {
            try {
                createdLayers[selectIndex].selected = true;
            } catch (ignoreSelectCreated) {}
        }

        return {
            camera: newCamera,
            nullCount: nullsToBake.length,
            createdLayers: createdLayers,
            sampleCount: times.length,
            propertyCount: targets.length,
            start: range.start,
            end: range.end,
            sampleRate: settings.sampleRate,
            skippedParentLinks: skippedParentLinks
        };
    }

    function formatError(error, stage) {
        var message = "Bake failed";
        if (stage) {
            message += " during: " + stage;
        }
        message += ".\n\n";

        if (error && error.message) {
            message += error.message;
        } else {
            message += error;
        }

        try {
            if (error.line) {
                message += "\n\nScript line: " + error.line;
            }
        } catch (ignoreLine) {}

        return message;
    }

    function main() {
        if (!app.project) {
            return fail("Open an After Effects project first.");
        }

        var comp = app.project.activeItem;
        if (!isComp(comp)) {
            return fail("Open the edit/master composition and select the retimed shot precomp layer.");
        }

        if (comp.selectedLayers.length !== 1) {
            return fail("Select exactly one time-remapped precomp layer in the active composition.");
        }

        var shotLayer = comp.selectedLayers[0];
        if (!(shotLayer instanceof AVLayer) || !isComp(shotLayer.source)) {
            return fail("The selected layer must be a precomp layer.");
        }

        var sourceComp = shotLayer.source;
        var cameras = collectCameras(sourceComp);
        var nulls = collectNulls(sourceComp);
        if (cameras.length === 0) {
            return fail("No camera layers were found inside the selected precomp.");
        }

        var settings = buildDialog(comp, shotLayer, sourceComp, cameras, nulls);
        if (!settings || !settings.camera) {
            return null;
        }

        var warnings = [];
        if (sourceComp.width !== comp.width || sourceComp.height !== comp.height) {
            warnings.push("The source and edit compositions have different dimensions.");
        }
        if (!nearlyEqual(sourceComp.pixelAspect, comp.pixelAspect, 0.0001)) {
            warnings.push("The source and edit compositions have different pixel aspect ratios.");
        }
        if (!shotLayerHasDefaultTransform(shotLayer, sourceComp, comp)) {
            warnings.push("The selected precomp is transformed, parented, or set as a 3D layer.");
        }
        var unsupportedParenting = false;
        var layersToCheck = [settings.camera];
        if (settings.includeNulls) {
            for (var parentNullIndex = 0; parentNullIndex < nulls.length; parentNullIndex++) {
                layersToCheck.push(nulls[parentNullIndex]);
            }
        }
        for (var parentCheckIndex = 0; parentCheckIndex < layersToCheck.length; parentCheckIndex++) {
            var checkedParent = null;
            try {
                checkedParent = layersToCheck[parentCheckIndex].parent;
            } catch (ignoreParentWarning) {}
            if (checkedParent && checkedParent.index !== settings.camera.index &&
                    !(settings.includeNulls && isNullLayer(checkedParent))) {
                unsupportedParenting = true;
                break;
            }
        }
        if (unsupportedParenting) {
            warnings.push("Some selected layers are parented to source layers that will not be baked. Those parent links will be skipped.");
        }
        if (!shotLayer.timeRemapEnabled) {
            warnings.push("Time Remapping is not enabled; layer start time and stretch will be used instead.");
        }
        if (!nearlyEqual(sourceComp.frameRate, comp.frameRate, 0.0001) &&
                settings.sampleRate < sourceComp.frameRate - 0.0001) {
            warnings.push(
                "The source comp is " + sourceComp.frameRate.toFixed(3) + " fps and the edit comp is " +
                comp.frameRate.toFixed(3) + " fps. A bake rate below the source FPS can reduce subframe accuracy."
            );
        }

        if (warnings.length > 0) {
            var warningText = SCRIPT_NAME + " " + SCRIPT_VERSION + "\n\n" +
                warnings.join("\n") +
                "\n\nContinue?";
            if (!confirm(warningText)) {
                return null;
            }
        }

        var stageState = { text: "Starting" };
        app.beginUndoGroup(SCRIPT_NAME);

        try {
            var result = bakeCameraAndNulls(comp, shotLayer, sourceComp, settings.camera, nulls, settings, stageState);
            if (!result) {
                return null;
            }

            var resultText =
                SCRIPT_NAME + " " + SCRIPT_VERSION + "\n\n" +
                "Created camera: " + result.camera.name + "\n" +
                "Created nulls: " + result.nullCount + "\n" +
                "Samples: " + result.sampleCount + "\n" +
                "Bake sampling: " + result.sampleRate.toFixed(3) + " fps\n" +
                "Samples per edit frame: " + (result.sampleRate / comp.frameRate).toFixed(3) + "\n" +
                "Baked properties: " + result.propertyCount + "\n" +
                "Range: " + result.start.toFixed(3) + "s - " + result.end.toFixed(3) + "s";
            if (result.skippedParentLinks > 0) {
                resultText += "\nSkipped parent links: " + result.skippedParentLinks;
            }
            alert(resultText);
        } catch (error) {
            if (error && error.message === "__USER_CANCELLED__") {
                alert(SCRIPT_NAME + " " + SCRIPT_VERSION + "\n\nBake cancelled. No camera or null layers were created.");
            } else {
                alert(SCRIPT_NAME + " " + SCRIPT_VERSION + "\n\n" + formatError(error, stageState.text));
            }
        } finally {
            app.endUndoGroup();
        }

        return null;
    }

    main();
})(this);
