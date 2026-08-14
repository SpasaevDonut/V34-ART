/*
 * CS:S V34 ADVANCED RECORDING TOOLS - After Effects Importer v1.0
 * Created by Contrastniy.
 *
 * HLAE camera import credits:
 * - mirv_camio (.cam): adapted from “HLAE CamIO To AE” v2.0 by
 *   xNWP:
 *   https://github.com/xNWP/HLAE-CamIO-To-AE
 * - mirv_camexport (.bvh): adapted from “HLAE BVH to AE Cam” v1.5 by
 *   msthavoc.
 * - HLAE game recording (.agr): binary format parsing is based on the
 *   official AdvancedFX advancedfx_import_gameRecord.py SFM importer:
 *   https://github.com/advancedfx/afx-sfm-scripts
 *   MIT License, Copyright (c) 2019 advancedfx.org.
 *   The lightweight mode imports player root positions only. The optional
 *   full mode imports all entity root transforms and AGR camera tracks as AE
 *   3D nulls. Indexed bone nulls and the global afxCam null can be excluded
 *   independently from the AGR section. AGR does not embed bone names or the
 *   model skeleton hierarchy, so bone nulls use their recorded numeric index.
 *
 * Run with File > Scripts > Run Script File in Adobe After Effects.
 * The importer reads the [take].json file written by ART.
 * It can import the traditional per-pass sequences / videos or a single
 * multilayer OpenEXR sequence from <take>/EXR.
 */

( function ArtTakeImporter( thisObject )
{
	var SCRIPT_VERSION = "1.0";
	var SCRIPT_NAME = "ART Take Importer v" + SCRIPT_VERSION;
	var VIDEO_EXTENSIONS = [ "mov", "mp4", "avi", "mxf", "mpg", "mpeg" ];

	function readTextFile( file )
	{
		if ( !file || !file.exists )
			throw new Error( "The selected take JSON file does not exist." );
		file.encoding = "UTF-8";
		if ( !file.open( "r" ) )
			throw new Error( "Could not open " + file.fsName );
		var text = file.read();
		file.close();
		return text;
	}

	function parseJson( text )
	{
		if ( typeof JSON !== "undefined" && JSON.parse )
			return JSON.parse( text );
		// Older ExtendScript engines do not expose JSON.parse. ART JSON contains
		// only data values, so the generated manifest remains compatible there.
		return eval( "(" + text + ")" );
	}

	function validateManifest( manifest )
	{
		if ( !manifest || manifest.schema !== "css-v34-art.take" )
			throw new Error( "This is not a CS:S V34 ART take manifest." );
		if ( !manifest.take || !manifest.capture || !manifest.passes )
			throw new Error( "The take manifest is incomplete." );
	}

	function normalizedFrameRate( manifest, fallback )
	{
		var rate = Number( manifest.capture.frame_rate );
		if ( !isFinite( rate ) || rate <= 0 )
			rate = Number( fallback );
		if ( !isFinite( rate ) || rate <= 0 )
			rate = 25;
		return rate;
	}

	function resolveTakeFolder( manifestFile, manifest )
	{
		if ( manifest.take.absolute_folder )
		{
			var recordedFolder = new Folder( manifest.take.absolute_folder );
			if ( recordedFolder.exists )
				return recordedFolder;
		}
		return manifestFile.parent;
	}

	function escapeRegExp( value )
	{
		return value.replace( /[.*+?^${}()|[\]\\]/g, "\\$&" );
	}

	function sequencePatternExpression( pattern )
	{
		var marker = "__ART_FRAME_NUMBER__";
		var marked = String( pattern ).replace( /%0?\d*d/, marker );
		var escaped = escapeRegExp( marked );
		return new RegExp( "^" + escaped.replace( marker, "\\d+" ) + "$", "i" );
	}

	function findSequenceFile( takeFolder, pass )
	{
		var passFolder = new Folder( takeFolder.fsName + "/" + pass.directory );
		if ( !passFolder.exists )
			return null;
		var expression = sequencePatternExpression( pass.filename_pattern );
		var files = passFolder.getFiles( function( item )
		{
			return item instanceof File && expression.test( item.name );
		} );
		files.sort( function( left, right )
		{
			var a = left.name.toLowerCase();
			var b = right.name.toLowerCase();
			return a < b ? -1 : a > b ? 1 : 0;
		} );
		return files.length ? files[0] : null;
	}

	function fileExtension( file )
	{
		var dot = file.name.lastIndexOf( "." );
		return dot >= 0 ? file.name.substring( dot + 1 ).toLowerCase() : "";
	}

	function videoNameMatchesPass( file, passName )
	{
		var dot = file.name.lastIndexOf( "." );
		var baseName = ( dot >= 0 ? file.name.substring( 0, dot ) : file.name ).toLowerCase();
		var pass = passName.toLowerCase();
		if ( baseName === pass )
			return true;
		if ( baseName.length > pass.length )
		{
			var suffix = baseName.substring( baseName.length - pass.length - 1 );
			if ( suffix === "_" + pass || suffix === "-" + pass )
				return true;
		}
		return baseName.indexOf( pass + "_" ) === 0 ||
			baseName.indexOf( "_" + pass + "_" ) >= 0;
	}

	function findVideoInFolder( folder, passName, requestedExtension )
	{
		if ( !folder || !folder.exists )
			return null;
		var requested = requestedExtension === "auto" ? null : requestedExtension;
		var matches = folder.getFiles( function( item )
		{
			if ( !( item instanceof File ) )
				return false;
			var extension = fileExtension( item );
			var supported = false;
			for ( var i = 0; i < VIDEO_EXTENSIONS.length; ++i )
			{
				if ( extension === VIDEO_EXTENSIONS[i] )
				{
					supported = true;
					break;
				}
			}
			if ( !supported || ( requested && extension !== requested ) )
				return false;
			return videoNameMatchesPass( item, passName );
		} );
		matches.sort( function( left, right )
		{
			var a = left.name.toLowerCase();
			var b = right.name.toLowerCase();
			return a < b ? -1 : a > b ? 1 : 0;
		} );
		return matches.length ? matches[0] : null;
	}

	function findVideoFile( takeFolder, pass, requestedExtension )
	{
		var passFolder = new Folder( takeFolder.fsName + "/" + pass.directory );
		return findVideoInFolder( passFolder, pass.name, requestedExtension ) ||
			findVideoInFolder( takeFolder, pass.name, requestedExtension );
	}


	function findMultilayerExrFile( takeFolder )
	{
		var exrFolder = new Folder( takeFolder.fsName + "/EXR" );
		if ( !exrFolder.exists )
			return null;
		var files = exrFolder.getFiles( function( item )
		{
			return item instanceof File && fileExtension( item ) === "exr";
		} );
		files.sort( function( left, right )
		{
			var a = left.name.toLowerCase();
			var b = right.name.toLowerCase();
			return a < b ? -1 : a > b ? 1 : 0;
		} );
		return files.length ? files[0] : null;
	}

	function snapshotProjectItems()
	{
		var result = [];
		for ( var i = 1; i <= app.project.numItems; ++i )
			result.push( app.project.item( i ) );
		return result;
	}

	function isValidProjectItem( item )
	{
		if ( !item )
			return false;
		try
		{
			// Reading id and name forces ExtendScript to validate the native AE object.
			var itemId = item.id;
			var itemName = item.name;
			return typeof itemId !== "undefined" && typeof itemName !== "undefined";
		}
		catch ( invalidProjectItemError )
		{
			return false;
		}
	}

	function projectItemWasInSnapshot( item, snapshot )
	{
		if ( !isValidProjectItem( item ) )
			return false;
		for ( var i = 0; i < snapshot.length; ++i )
		{
			var previous = snapshot[i];
			if ( !isValidProjectItem( previous ) )
				continue;
			if ( previous === item )
				return true;
			try
			{
				if ( previous.id === item.id )
					return true;
			}
			catch ( ignoredProjectItemIdError ) {}
		}
		return false;
	}

	function projectItemsCreatedSince( snapshot )
	{
		var result = [];
		for ( var i = 1; i <= app.project.numItems; ++i )
		{
			var item = app.project.item( i );
			if ( !projectItemWasInSnapshot( item, snapshot ) )
				result.push( item );
		}
		return result;
	}

	function bestCompositionInItems( items )
	{
		var best = null;
		var bestLayerCount = -1;
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			if ( !( item instanceof CompItem ) )
				continue;
			var layerCount = Number( item.numLayers ) || 0;
			if ( !best || layerCount > bestLayerCount )
			{
				best = item;
				bestLayerCount = layerCount;
			}
		}
		return best;
	}

	function firstExrFootageInItems( items )
	{
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			if ( !( item instanceof FootageItem ) || !item.mainSource )
				continue;
			try
			{
				if ( item.mainSource.file &&
					fileExtension( item.mainSource.file ) === "exr" )
					return item;
			}
			catch ( ignoredExrSourceError ) {}
		}
		return null;
	}

	function layerHasExtractor( layer )
	{
		if ( !layer )
			return false;
		var effects = null;
		try { effects = layer.property( "ADBE Effect Parade" ); }
		catch ( ignoredEffectsLookupError ) {}
		if ( !effects )
			return false;
		for ( var i = 1; i <= effects.numProperties; ++i )
		{
			var effect = effects.property( i );
			if ( !effect )
				continue;
			var matchName = "";
			var name = "";
			try { matchName = String( effect.matchName ).toLowerCase(); }
			catch ( ignoredExtractorMatchName ) {}
			try { name = String( effect.name ).toLowerCase(); }
			catch ( ignoredExtractorName ) {}
			// Current and older fnord / Adobe builds do not always expose the
			// same matchName, so accept any effect name containing EXtractoR.
			if ( matchName.indexOf( "extractor" ) >= 0 ||
				name.indexOf( "extractor" ) >= 0 )
				return true;
		}
		return false;
	}

	function compContainsExtractor( comp, depth )
	{
		if ( !comp || !( comp instanceof CompItem ) )
			return false;
		var remainingDepth = Number( depth );
		if ( !isFinite( remainingDepth ) )
			remainingDepth = 0;
		for ( var i = 1; i <= comp.numLayers; ++i )
		{
			var layer = comp.layer( i );
			if ( layerHasExtractor( layer ) )
				return true;
			if ( remainingDepth > 0 )
			{
				var source = null;
				try { source = layer.source; }
				catch ( ignoredNestedSourceError ) {}
				if ( source instanceof CompItem &&
					compContainsExtractor( source, remainingDepth - 1 ) )
					return true;
			}
		}
		return false;
	}

	function layerIsExtractorBacked( layer )
	{
		if ( layerHasExtractor( layer ) )
			return true;
		var source = null;
		try { source = layer.source; }
		catch ( ignoredExtractorSourceError ) {}
		return source instanceof CompItem && compContainsExtractor( source, 3 );
	}

	function countExtractorBackedLayers( comp )
	{
		if ( !comp || !( comp instanceof CompItem ) )
			return 0;
		var count = 0;
		for ( var i = 1; i <= comp.numLayers; ++i )
		{
			if ( layerIsExtractorBacked( comp.layer( i ) ) )
				++count;
		}
		return count;
	}

	function bestExtractorCompositionInItems( items )
	{
		var best = null;
		var bestBackedCount = -1;
		var bestLayerCount = -1;
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			if ( !( item instanceof CompItem ) )
				continue;
			// ProEXR commonly builds an assembly comp whose layers are nested
			// source comps. EXtractoR is inside those source comps, not directly
			// on the assembly layers, so detection must recurse through sources.
			var backedCount = countExtractorBackedLayers( item );
			var layerCount = Number( item.numLayers ) || 0;
			if ( !best || backedCount > bestBackedCount ||
				( backedCount === bestBackedCount && layerCount > bestLayerCount ) )
			{
				best = item;
				bestBackedCount = backedCount;
				bestLayerCount = layerCount;
			}
		}
		return best;
	}

	function conformExrFootageItems( items, frameRate, footageFolder )
	{
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			if ( !( item instanceof FootageItem ) || !item.mainSource )
				continue;
			try
			{
				if ( !item.mainSource.file ||
					fileExtension( item.mainSource.file ) !== "exr" )
					continue;
				item.mainSource.conformFrameRate = frameRate;
				item.parentFolder = footageFolder;
			}
			catch ( ignoredExrConformError ) {}
		}
	}

	function synchronizeExrCompositionItems( items, frameRate, duration )
	{
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			if ( !( item instanceof CompItem ) )
				continue;
			try { item.frameRate = frameRate; }
			catch ( ignoredExrNestedRateError ) {}
			try { item.duration = duration; }
			catch ( ignoredExrNestedDurationError ) {}
			try
			{
				item.workAreaStart = 0;
				item.workAreaDuration = duration;
			}
			catch ( ignoredExrNestedWorkAreaError ) {}
			for ( var layerIndex = 1; layerIndex <= item.numLayers; ++layerIndex )
			{
				try
				{
					var layer = item.layer( layerIndex );
					layer.inPoint = 0;
					layer.outPoint = duration;
				}
				catch ( ignoredExrNestedLayerRangeError ) {}
			}
		}
	}

	function deselectAllProjectItems()
	{
		for ( var i = 1; i <= app.project.numItems; ++i )
		{
			try { app.project.item( i ).selected = false; }
			catch ( ignoredProjectSelectionError ) {}
		}
	}


	function countManifestPassLayers( comp, manifest )
	{
		if ( !comp || !( comp instanceof CompItem ) || !manifest )
			return 0;
		var found = {};
		var count = 0;
		for ( var i = 1; i <= comp.numLayers; ++i )
		{
			var pass = passForExrLayer( manifest, comp.layer( i ) );
			if ( pass && !found[pass.name] )
			{
				found[pass.name] = true;
				++count;
			}
		}
		return count;
	}

	function bestManifestExrComposition( items, manifest, preferred )
	{
		var best = isValidProjectItem( preferred ) && preferred instanceof CompItem ?
			preferred : null;
		var bestMatches = best ? countManifestPassLayers( best, manifest ) : -1;
		var bestLayerCount = best ? Number( best.numLayers ) || 0 : -1;
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			if ( !( item instanceof CompItem ) )
				continue;
			var matches = countManifestPassLayers( item, manifest );
			var layerCount = Number( item.numLayers ) || 0;
			if ( !best || matches > bestMatches ||
				( matches === bestMatches && layerCount > bestLayerCount ) )
			{
				best = item;
				bestMatches = matches;
				bestLayerCount = layerCount;
			}
		}
		return best;
	}

	function passForExrText( manifest, text )
	{
		var normalizedText = normalizedChannelName( String( text || "" ) );
		var best = null;
		var bestLength = -1;
		for ( var i = 0; i < manifest.passes.length; ++i )
		{
			var pass = manifest.passes[i];
			var normalizedPass = normalizedChannelName( pass.name );
			if ( normalizedPass && normalizedText.indexOf( normalizedPass ) >= 0 &&
				normalizedPass.length > bestLength )
			{
				best = pass;
				bestLength = normalizedPass.length;
			}
		}
		return best;
	}

	function passForGeneratedExrComp( manifest, comp )
	{
		if ( !comp || !( comp instanceof CompItem ) )
			return null;
		var pass = passForExrText( manifest, comp.name );
		if ( pass )
			return pass;
		for ( var i = 1; i <= comp.numLayers; ++i )
		{
			pass = passForExrLayer( manifest, comp.layer( i ) );
			if ( pass )
				return pass;
		}
		return null;
	}

	function buildAssemblyFromGeneratedExrPassComps( items, manifest, name,
		width, height, duration, frameRate )
	{
		var byPass = {};
		var count = 0;
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !( item instanceof CompItem ) || !compContainsExtractor( item, 4 ) )
				continue;
			var pass = passForGeneratedExrComp( manifest, item );
			if ( !pass || byPass[pass.name] )
				continue;
			byPass[pass.name] = { comp: item, pass: pass };
			++count;
		}
		if ( count <= 0 )
			return null;

		var assembly = app.project.items.addComp(
			name + " - ART", width, height, 1, duration, frameRate );
		var preferredOrder = [
			"objectid", "depth", "normal", "clear",
			"clear-noplayers", "players", "viewmodel"
		];
		var added = {};
		for ( var orderIndex = preferredOrder.length - 1; orderIndex >= 0; --orderIndex )
		{
			var preferred = byPass[preferredOrder[orderIndex]];
			if ( !preferred )
				continue;
			var layer = assembly.layers.add( preferred.comp );
			layer.name = preferred.pass.name;
			added[preferred.pass.name] = true;
		}
		for ( var passName in byPass )
		{
			if ( added[passName] )
				continue;
			var extra = byPass[passName];
			var extraLayer = assembly.layers.add( extra.comp );
			extraLayer.name = extra.pass.name;
		}
		return { comp: assembly, extractorCount: count, items: [ assembly ] };
	}

	function replaceImportedExrStillsWithSequence( items, firstFile,
		frameRate, warnings )
	{
		var replaced = 0;
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !( item instanceof FootageItem ) || !item.mainSource )
				continue;
			var sourceFile = null;
			try { sourceFile = item.mainSource.file; }
			catch ( ignoredImportedExrFileError ) {}
			if ( !sourceFile || fileExtension( sourceFile ) !== "exr" )
				continue;
			var originalName = "";
			try { originalName = item.name; }
			catch ( ignoredImportedExrNameError ) {}
			try
			{
				// Importing the numbered EXR with sequence=true and importAs=COMP
				// does not consistently trigger AE's layered-EXR comp builder.
				// Build the layered comp from one frame first, then preserve all
				// generated EXtractoR settings while replacing its footage source.
				item.replaceWithSequence( firstFile, false );
				if ( originalName )
					item.name = originalName;
				try { item.mainSource.conformFrameRate = frameRate; }
				catch ( ignoredReplacedExrRateError ) {}
				++replaced;
			}
			catch ( replaceSequenceError )
			{
				warnings.push( "Could not convert generated EXR footage to the full " +
					"sequence: " + replaceSequenceError.toString() );
			}
		}
		return replaced;
	}


	function proExrLayerCompCommandNames()
	{
		return [
			"Create ProEXR Layer Comps",
			"Create OpenEXR Layer Comps",
			"Create ProEXR Layer Compositions"
		];
	}

	function findProExrLayerCompsCommandId()
	{
		var names = proExrLayerCompCommandNames();
		for ( var i = 0; i < names.length; ++i )
		{
			var commandId = 0;
			try { commandId = app.findMenuCommandId( names[i] ); }
			catch ( ignoredProExrMenuLookupError ) {}
			if ( commandId )
				return commandId;
		}
		return 0;
	}

	function requireProExrLayerCompsCommandId()
	{
		var commandId = findProExrLayerCompsCommandId();
		if ( commandId )
			return commandId;

		// This command is contextual in some AE builds: it may not be exposed
		// until an EXR footage item or EXR layer is selected. Call this function
		// only after the builder comp and layer have been activated.
		var preferenceAlreadyEnabled = false;
		try
		{
			preferenceAlreadyEnabled = app.preferences.havePref(
				"OpenEXR", "File Menu Item" ) &&
				app.preferences.getPrefAsLong( "OpenEXR", "File Menu Item" ) !== 0;
		}
		catch ( ignoredPreferenceReadError ) {}

		if ( !preferenceAlreadyEnabled )
		{
			try
			{
				app.preferences.savePrefAsLong( "OpenEXR", "File Menu Item", 1 );
				app.preferences.saveToDisk();
			}
			catch ( preferenceWriteError )
			{
				throw new Error( "Could not enable After Effects' native OpenEXR " +
					"layer-comp command: " + preferenceWriteError.toString() );
			}
			throw new Error(
				"Restart After Effects once, then run the importer again. " +
				"Create ProEXR Layer Comps has just been enabled." );
		}

		throw new Error(
			"Create ProEXR Layer Comps could not be resolved after selecting the " +
			"EXR layer. Open the File menu once and confirm that the command is " +
			"visible, then run the importer again." );
	}

	function tryCreateProExrLayerComps( footageItem, manifest, width, height,
		duration, frameRate, warnings )
	{
		if ( !footageItem )
			throw new Error( "No EXR footage item is available for pass expansion." );
		var expectedPassCount = orderedRecordedExrPasses( manifest ).length;

		var builderComp = app.project.items.addComp(
			"ART EXR Layer Builder", width, height, 1, duration, frameRate );
		var builderLayer = builderComp.layers.add( footageItem );
		builderLayer.name = footageItem.name;
		try
		{
			builderLayer.inPoint = 0;
			builderLayer.outPoint = duration;
		}
		catch ( ignoredBuilderLayerRangeError ) {}

		// Snapshot after creating the temporary comp. The command may either
		// convert this comp into the assembly comp or create separate source and
		// assembly comps. Both forms are supported below.
		var beforeItems = snapshotProjectItems();
		try
		{
			deselectAllProjectItems();
			try { footageItem.selected = true; }
			catch ( ignoredExrProjectSelectionError ) {}
			for ( var layerIndex = 1; layerIndex <= builderComp.numLayers; ++layerIndex )
				builderComp.layer( layerIndex ).selected = false;
			builderLayer.selected = true;
			builderComp.openInViewer();
			try { app.refresh(); }
			catch ( ignoredBuilderRefreshError ) {}
			var commandId = requireProExrLayerCompsCommandId();
			app.executeCommand( commandId );
		}
		catch ( menuError )
		{
			try { builderComp.remove(); }
			catch ( ignoredFailedBuilderCompRemoveError ) {}
			throw new Error( "Could not run Create ProEXR Layer Comps: " +
				menuError.toString() );
		}

		var createdItems = projectItemsCreatedSince( beforeItems );
		var candidates = [];
		if ( isValidProjectItem( builderComp ) )
			candidates.push( builderComp );
		for ( var createdIndex = 0; createdIndex < createdItems.length; ++createdIndex )
		{
			if ( isValidProjectItem( createdItems[createdIndex] ) )
				candidates.push( createdItems[createdIndex] );
		}

		var comp = bestManifestExrComposition( candidates, manifest, builderComp );
		var manifestCount = comp ? countManifestPassLayers( comp, manifest ) : 0;
		var extractorCount = comp ? countExtractorBackedLayers( comp ) : 0;

		// Some AE builds create only the individual source comps. Assemble those
		// source comps ourselves while preserving the EXtractoR settings written
		// by the native OpenEXR component.
		if ( manifestCount < expectedPassCount )
		{
			var assemblyResult = buildAssemblyFromGeneratedExrPassComps(
				candidates, manifest, footageItem.name,
				width, height, duration, frameRate );
			if ( assemblyResult )
			{
				var assembledManifestCount = countManifestPassLayers(
					assemblyResult.comp, manifest );
				if ( assembledManifestCount > manifestCount )
				{
					comp = assemblyResult.comp;
					manifestCount = assembledManifestCount;
					extractorCount = Math.max(
						extractorCount, assemblyResult.extractorCount );
				}
				for ( var assemblyIndex = 0;
					assemblyIndex < assemblyResult.items.length; ++assemblyIndex )
					createdItems.push( assemblyResult.items[assemblyIndex] );
			}
		}

		if ( manifestCount <= 0 )
		{
			var extractorComp = bestExtractorCompositionInItems( candidates );
			if ( extractorComp )
			{
				comp = extractorComp;
				extractorCount = Math.max(
					extractorCount, countExtractorBackedLayers( extractorComp ) );
				manifestCount = countManifestPassLayers( extractorComp, manifest );
			}
		}

		if ( expectedPassCount > 0 && manifestCount < expectedPassCount )
			warnings.push( "Create ProEXR Layer Comps produced " + manifestCount +
				" of " + expectedPassCount + " ART pass layers." );

		if ( manifestCount <= 0 || extractorCount <= 0 )
		{
			throw new Error(
				"After Effects ran Create ProEXR Layer Comps, but no configured " +
				"ART pass layers were produced. Verify that the EXR contains channel " +
				"groups named like pass.R, pass.G and pass.B." );
		}

		if ( !isValidProjectItem( comp ) )
			throw new Error( "The native OpenEXR builder returned an invalid composition." );

		if ( comp !== builderComp && isValidProjectItem( builderComp ) )
		{
			try { builderComp.remove(); }
			catch ( ignoredUnusedBuilderCompRemoveError ) {}
		}

		return {
			comp: comp,
			extractorCount: Math.max( manifestCount, extractorCount ),
			manifestCount: manifestCount,
			items: createdItems
		};
	}

	function importExrSequenceAsComposition( firstFile, manifest,
		frameRate, warnings )
	{
		var options = new ImportOptions( firstFile );
		// Important: layered OpenEXR composition import is initialized from a
		// single frame. The generated EXR footage item is converted to a sequence
		// afterwards with replaceWithSequence(), preserving EXtractoR mappings.
		options.sequence = false;
		options.forceAlphabetical = false;
		var canImportComposition = false;
		try { canImportComposition = options.canImportAs( ImportAsType.COMP ); }
		catch ( ignoredCanImportExrCompositionError ) {}
		if ( !canImportComposition )
			return null;

		options.importAs = ImportAsType.COMP;
		var beforeItems = snapshotProjectItems();
		var returnedItem = null;
		try { returnedItem = app.project.importFile( options ); }
		catch ( compositionImportError )
		{
			warnings.push( "Native multilayer EXR composition import failed: " +
				compositionImportError.toString() );
			return null;
		}
		var createdItems = projectItemsCreatedSince( beforeItems );
		var preferredComp = returnedItem instanceof CompItem ? returnedItem : null;
		var comp = bestManifestExrComposition(
			createdItems, manifest, preferredComp );
		var sourceFootage = returnedItem instanceof FootageItem ? returnedItem :
			firstExrFootageInItems( createdItems );
		var replacedCount = replaceImportedExrStillsWithSequence(
			createdItems, firstFile, frameRate, warnings );
		return {
			comp: comp,
			sourceFootage: sourceFootage,
			items: createdItems,
			extractorCount: comp ? Math.max(
				countManifestPassLayers( comp, manifest ),
				countExtractorBackedLayers( comp ) ) : 0,
			replacedSequenceCount: replacedCount
		};
	}

	function importExrSequenceAsFootage( firstFile )
	{
		var options = new ImportOptions( firstFile );
		options.sequence = true;
		options.forceAlphabetical = false;
		try
		{
			if ( options.canImportAs( ImportAsType.FOOTAGE ) )
				options.importAs = ImportAsType.FOOTAGE;
		}
		catch ( ignoredExrFootageImportCheck ) {}
		var beforeItems = snapshotProjectItems();
		var returnedItem = app.project.importFile( options );
		var createdItems = projectItemsCreatedSince( beforeItems );
		var sourceFootage = returnedItem instanceof FootageItem ? returnedItem :
			firstExrFootageInItems( createdItems );
		return { sourceFootage: sourceFootage, items: createdItems };
	}

	function projectItemNameStartsWith( item, prefix )
	{
		var itemName = "";
		try { itemName = String( item.name ); }
		catch ( ignoredProjectItemNameError ) {}
		return itemName.indexOf( prefix ) === 0;
	}

	function prefixProjectItemName( item, takeName )
	{
		if ( !item || !takeName || projectItemNameStartsWith( item, takeName + " - " ) )
			return;
		try { item.name = takeName + " - " + item.name; }
		catch ( ignoredProjectItemPrefixError ) {}
	}

	function renameGeneratedExrComp( comp, manifest, takeName )
	{
		if ( !comp || !( comp instanceof CompItem ) )
			return;

		var lowerName = "";
		try { lowerName = String( comp.name ).toLowerCase(); }
		catch ( ignoredGeneratedCompNameError ) {}
		// Contact sheets contain every pass and would otherwise be mistaken for
		// whichever nested pass is encountered first. Identify them by name first.
		if ( lowerName.indexOf( "contact sheet" ) >= 0 )
		{
			try { comp.name = takeName + " - EXR contact sheet"; }
			catch ( ignoredContactSheetRenameError ) {}
			return;
		}

		var pass = passForGeneratedExrComp( manifest, comp );
		if ( pass )
		{
			try { comp.name = takeName + " - " + pass.name + " source"; }
			catch ( ignoredGeneratedPassCompRenameError ) {}
			return;
		}

		prefixProjectItemName( comp, takeName );
	}

	function organizeExrItems( items, compositionFolder, sourceCompFolder,
		footageFolder, masterComp, manifest, takeName )
	{
		var generatedFolders = [];
		for ( var i = 0; i < items.length; ++i )
		{
			var item = items[i];
			if ( !isValidProjectItem( item ) )
				continue;
			try
			{
				if ( item instanceof FootageItem )
				{
					item.parentFolder = footageFolder;
				}
				else if ( item instanceof CompItem )
				{
					if ( item === masterComp )
						item.parentFolder = compositionFolder;
					else
					{
						renameGeneratedExrComp( item, manifest, takeName );
						item.parentFolder = sourceCompFolder;
					}
				}
				else if ( item instanceof FolderItem )
				{
					generatedFolders.push( item );
				}
			}
			catch ( ignoredExrOrganizationError ) {}
		}
		return generatedFolders;
	}

	function removeEmptyGeneratedExrFolders( generatedFolders,
		compositionFolder, sourceCompFolder, footageFolder )
	{
		// Cleanup is deliberately last. Removing native folders earlier leaves
		// invalid ExtendScript references in the generated-item snapshot.
		for ( var folderIndex = generatedFolders.length - 1;
			folderIndex >= 0; --folderIndex )
		{
			var generatedFolder = generatedFolders[folderIndex];
			if ( !isValidProjectItem( generatedFolder ) )
				continue;
			try
			{
				if ( generatedFolder !== sourceCompFolder &&
					generatedFolder !== compositionFolder &&
					generatedFolder !== footageFolder &&
					generatedFolder.numItems === 0 )
					generatedFolder.remove();
			}
			catch ( ignoredEmptyGeneratedFolderRemoveError ) {}
		}
	}


	function orderedRecordedExrPasses( manifest )
	{
		var available = [];
		var hasPositiveFileCount = false;
		for ( var i = 0; i < manifest.passes.length; ++i )
		{
			var pass = manifest.passes[i];
			if ( !pass || !pass.name )
				continue;
			if ( Number( pass.files ) > 0 )
				hasPositiveFileCount = true;
			available.push( pass );
		}

		var filtered = [];
		for ( var availableIndex = 0; availableIndex < available.length; ++availableIndex )
		{
			var availablePass = available[availableIndex];
			if ( hasPositiveFileCount && Number( availablePass.files ) <= 0 )
				continue;
			filtered.push( availablePass );
		}

		var preferredNames = [
			"objectid", "depth", "normal", "clear",
			"clear-noplayers", "players", "viewmodel"
		];
		var result = [];
		var added = {};
		for ( var preferredIndex = 0; preferredIndex < preferredNames.length; ++preferredIndex )
		{
			for ( var passIndex = 0; passIndex < filtered.length; ++passIndex )
			{
				var candidate = filtered[passIndex];
				if ( candidate.name === preferredNames[preferredIndex] &&
					!added[candidate.name] )
				{
					result.push( candidate );
					added[candidate.name] = true;
				}
			}
		}
		for ( var extraIndex = 0; extraIndex < filtered.length; ++extraIndex )
		{
			var extra = filtered[extraIndex];
			if ( added[extra.name] )
				continue;
			result.push( extra );
			added[extra.name] = true;
		}
		return result;
	}

	function importMultilayerExr( takeFolder, compositionFolder,
		sourceCompFolder, footageFolder, name, width, height, duration,
		frameRate, createComposition, manifest, warnings )
	{
		var firstFile = findMultilayerExrFile( takeFolder );
		if ( !firstFile )
			throw new Error( "No OpenEXR sequence was found in " +
				new Folder( takeFolder.fsName + "/EXR" ).fsName + "." );

		var footageResult = importExrSequenceAsFootage( firstFile );
		var sourceFootage = footageResult.sourceFootage;
		if ( !sourceFootage )
			throw new Error( "After Effects did not return an OpenEXR footage item." );

		sourceFootage.name = name + " - Multilayer EXR";
		sourceFootage.parentFolder = footageFolder;
		try { sourceFootage.mainSource.conformFrameRate = frameRate; }
		catch ( ignoredNativeExrRateError ) {}

		var allCreatedItems = footageResult.items || [];
		var masterComp = null;
		var extractorLayerCount = 0;
		var manifestPassCount = 0;

		if ( createComposition )
		{
			var nativeResult = tryCreateProExrLayerComps(
				sourceFootage, manifest, width, height,
				duration, frameRate, warnings );
			masterComp = nativeResult.comp;
			extractorLayerCount = nativeResult.extractorCount;
			manifestPassCount = nativeResult.manifestCount || 0;
			for ( var nativeItemIndex = 0;
				nativeItemIndex < nativeResult.items.length; ++nativeItemIndex )
				allCreatedItems.push( nativeResult.items[nativeItemIndex] );
		}

		var generatedExrFolders = organizeExrItems(
			allCreatedItems, compositionFolder, sourceCompFolder,
			footageFolder, masterComp, manifest, name );
		conformExrFootageItems( allCreatedItems, frameRate, footageFolder );
		synchronizeExrCompositionItems( allCreatedItems, frameRate, duration );
		removeEmptyGeneratedExrFolders( generatedExrFolders,
			compositionFolder, sourceCompFolder, footageFolder );

		if ( masterComp )
		{
			masterComp.name = name + " - ART";
			masterComp.parentFolder = compositionFolder;
			try { masterComp.width = width; }
			catch ( ignoredExrCompWidthError ) {}
			try { masterComp.height = height; }
			catch ( ignoredExrCompHeightError ) {}
			try { masterComp.frameRate = frameRate; }
			catch ( ignoredExrCompRateError ) {}
			masterComp.duration = duration;
			try
			{
				masterComp.workAreaStart = 0;
				masterComp.workAreaDuration = duration;
			}
			catch ( ignoredExrWorkAreaError ) {}
		}

		var expectedPasses = orderedRecordedExrPasses( manifest ).length;
		if ( createComposition && expectedPasses > 0 &&
			manifestPassCount < expectedPasses )
		{
			warnings.push( "The native OpenEXR builder matched " +
				manifestPassCount + " of " + expectedPasses +
				" recorded ART passes. Check the EXR channel prefixes." );
		}

		return {
			masterComp: masterComp,
			sourceFootage: sourceFootage,
			expanded: createComposition && extractorLayerCount > 0,
			extractorLayerCount: extractorLayerCount,
			file: firstFile
		};
	}


	function importMedia( file, sequence, frameRate )
	{
		var options = new ImportOptions( file );
		if ( sequence )
		{
			options.sequence = true;
			options.forceAlphabetical = false;
		}
		var item = app.project.importFile( options );
		if ( item && item.mainSource && frameRate > 0 )
		{
			try { item.mainSource.conformFrameRate = frameRate; }
			catch ( ignoredFrameRateError ) {}
		}
		return item;
	}

	function splitWords( line )
	{
		var trimmed = String( line ).replace( /^\s+|\s+$/g, "" );
		return trimmed ? trimmed.split( /\s+/ ) : [];
	}

	function addCameraFilesFromFolder( folder, suffix, files )
	{
		if ( !folder || !folder.exists )
			return;
		var matches = folder.getFiles( function( item )
		{
			if ( !( item instanceof File ) )
				return false;
			var name = item.name.toLowerCase();
			return name.length > suffix.length &&
				name.substring( name.length - suffix.length ) === suffix;
		} );
		for ( var i = 0; i < matches.length; ++i )
			files.push( matches[i] );
	}

	function cameraFileRank( file, extension )
	{
		var name = file.name.toLowerCase();
		var score = 0;
		if ( extension === "cam" )
		{
			if ( name === "mirv_camio.cam" ) score += 100;
			if ( name.indexOf( "mirv_camio" ) >= 0 ) score += 60;
			if ( name.indexOf( "camio" ) >= 0 ) score += 30;
		}
		else if ( extension === "bvh" )
		{
			if ( name === "mirv_camexport.bvh" ) score += 100;
			if ( name.indexOf( "mirv_camexport" ) >= 0 ) score += 60;
			if ( name.indexOf( "camexport" ) >= 0 ) score += 30;
		}
		if ( name.indexOf( "camera" ) >= 0 ) score += 10;
		if ( name.indexOf( "cam" ) >= 0 ) score += 5;
		return score;
	}

	function findCameraFile( takeFolder, extension )
	{
		var normalizedExtension = String( extension ).toLowerCase();
		var suffix = "." + normalizedExtension;
		var files = [];
		addCameraFilesFromFolder( takeFolder, suffix, files );

		// ART / HLAE camera files are commonly kept in a camera-named child folder.
		// Restricting the search avoids scanning image-sequence folders with thousands of files.
		var childFolders = takeFolder.getFiles( function( item )
		{
			if ( !( item instanceof Folder ) )
				return false;
			var name = item.name.toLowerCase();
			return name.indexOf( "cam" ) >= 0 || name.indexOf( "hlae" ) >= 0 ||
				name.indexOf( "motion" ) >= 0;
		} );
		for ( var folderIndex = 0; folderIndex < childFolders.length; ++folderIndex )
			addCameraFilesFromFolder( childFolders[folderIndex], suffix, files );

		files.sort( function( left, right )
		{
			var rankDifference = cameraFileRank( right, normalizedExtension ) -
				cameraFileRank( left, normalizedExtension );
			if ( rankDifference )
				return rankDifference;
			var a = left.fsName.toLowerCase();
			var b = right.fsName.toLowerCase();
			return a < b ? -1 : a > b ? 1 : 0;
		} );
		return files.length ? files[0] : null;
	}


	function agrFileRank( file, takeName )
	{
		var name = file.name.toLowerCase();
		var normalizedTakeName = String( takeName || "" ).toLowerCase();
		var score = 0;
		if ( normalizedTakeName && name === normalizedTakeName + ".agr" ) score += 140;
		if ( name === "mirv_agr.agr" || name === "art_agr.agr" ) score += 120;
		if ( name === "afxgamerecord.agr" ) score += 110;
		if ( name.indexOf( "mirv_agr" ) >= 0 || name.indexOf( "art_agr" ) >= 0 ) score += 60;
		if ( name.indexOf( "gamerecord" ) >= 0 ) score += 50;
		if ( name.indexOf( "player" ) >= 0 ) score += 25;
		if ( normalizedTakeName && name.indexOf( normalizedTakeName ) >= 0 ) score += 20;
		return score;
	}

	function findAgrFile( takeFolder, takeName )
	{
		var files = [];
		addCameraFilesFromFolder( takeFolder, ".agr", files );

		// AGR files are normally written into the take root or a small HLAE / AGR
		// child folder. Do not recurse into pass folders containing image sequences.
		var childFolders = takeFolder.getFiles( function( item )
		{
			if ( !( item instanceof Folder ) )
				return false;
			var name = item.name.toLowerCase();
			return name.indexOf( "agr" ) >= 0 || name.indexOf( "hlae" ) >= 0 ||
				name.indexOf( "game" ) >= 0 || name.indexOf( "record" ) >= 0 ||
				name.indexOf( "motion" ) >= 0 || name.indexOf( "player" ) >= 0;
		} );
		for ( var folderIndex = 0; folderIndex < childFolders.length; ++folderIndex )
			addCameraFilesFromFolder( childFolders[folderIndex], ".agr", files );

		files.sort( function( left, right )
		{
			var rankDifference = agrFileRank( right, takeName ) -
				agrFileRank( left, takeName );
			if ( rankDifference )
				return rankDifference;
			var a = left.fsName.toLowerCase();
			var b = right.fsName.toLowerCase();
			return a < b ? -1 : a > b ? 1 : 0;
		} );
		return files.length ? files[0] : null;
	}

	function AgrBinaryReader( file )
	{
		this.file = file;
	}

	AgrBinaryReader.prototype.readByte = function()
	{
		var value = this.file.read( 1 );
		if ( !value || value.length < 1 )
			return null;
		return value.charCodeAt( 0 ) & 255;
	};

	AgrBinaryReader.prototype.readBytes = function( count )
	{
		var value = this.file.read( count );
		if ( !value || value.length < count )
			throw new Error( "Unexpected end of AGR file." );
		return value;
	};

	AgrBinaryReader.prototype.readInt32 = function()
	{
		var b0 = this.readByte();
		if ( b0 === null )
			return null;
		var b1 = this.readByte();
		var b2 = this.readByte();
		var b3 = this.readByte();
		if ( b1 === null || b2 === null || b3 === null )
			throw new Error( "Unexpected end of AGR integer." );
		return ( b0 | ( b1 << 8 ) | ( b2 << 16 ) | ( b3 << 24 ) );
	};

	AgrBinaryReader.prototype.readFloat32 = function()
	{
		var b0 = this.readByte();
		if ( b0 === null )
			return null;
		var b1 = this.readByte();
		var b2 = this.readByte();
		var b3 = this.readByte();
		if ( b1 === null || b2 === null || b3 === null )
			throw new Error( "Unexpected end of AGR float." );

		// Decode little-endian IEEE-754 without DataView, which ExtendScript lacks.
		var bits = b0 + b1 * 256 + b2 * 65536 + b3 * 16777216;
		var sign = bits >= 2147483648 ? -1 : 1;
		if ( sign < 0 )
			bits -= 2147483648;
		var exponent = Math.floor( bits / 8388608 );
		var mantissa = bits - exponent * 8388608;
		if ( exponent === 255 )
			return mantissa ? NaN : sign * Infinity;
		if ( exponent === 0 )
			return sign * Math.pow( 2, -126 ) * ( mantissa / 8388608 );
		return sign * Math.pow( 2, exponent - 127 ) *
			( 1 + mantissa / 8388608 );
	};

	AgrBinaryReader.prototype.readBool = function()
	{
		var value = this.readByte();
		if ( value === null )
			throw new Error( "Unexpected end of AGR boolean." );
		return value !== 0;
	};

	AgrBinaryReader.prototype.readCString = function()
	{
		var result = "";
		for ( var i = 0; i < 1048576; ++i )
		{
			var value = this.readByte();
			if ( value === null )
				throw new Error( "Unexpected end of AGR string." );
			if ( value === 0 )
				return result;
			result += String.fromCharCode( value );
		}
		throw new Error( "AGR string exceeds the safety limit." );
	};

	AgrBinaryReader.prototype.readVector = function()
	{
		return [ this.readFloat32(), this.readFloat32(), this.readFloat32() ];
	};

	AgrBinaryReader.prototype.skipFloats = function( count )
	{
		for ( var i = 0; i < count; ++i )
			this.readFloat32();
	};

	AgrBinaryReader.prototype.readMatrix3x4Position = function()
	{
		var position = [ 0, 0, 0 ];
		for ( var i = 0; i < 12; ++i )
		{
			var value = this.readFloat32();
			if ( i === 3 ) position[0] = value;
			else if ( i === 7 ) position[1] = value;
			else if ( i === 11 ) position[2] = value;
		}
		return position;
	};

	function AgrDictionary( reader )
	{
		this.reader = reader;
		this.values = [];
		this.peeked = null;
		this.hasPeeked = false;
	}

	AgrDictionary.prototype.read = function()
	{
		if ( this.hasPeeked )
		{
			var value = this.peeked;
			this.peeked = null;
			this.hasPeeked = false;
			return value;
		}
		var index = this.reader.readInt32();
		if ( index === null )
			return null;
		if ( index === -1 )
		{
			var text = this.reader.readCString();
			this.values.push( text );
			return text;
		}
		if ( index < 0 || index >= this.values.length )
			throw new Error( "AGR dictionary index " + index + " is invalid." );
		return this.values[index];
	};

	AgrDictionary.prototype.peek = function( expected )
	{
		if ( !this.hasPeeked )
		{
			this.peeked = this.read();
			this.hasPeeked = true;
		}
		if ( this.peeked === expected )
		{
			this.peeked = null;
			this.hasPeeked = false;
			return true;
		}
		return false;
	};

	function agrIsPlayerModel( modelName )
	{
		var normalized = String( modelName || "" ).replace( /\\/g, "/" ).toLowerCase();
		return normalized.indexOf( "models/player/" ) === 0 ||
			normalized.indexOf( "/models/player/" ) >= 0;
	}

	function agrModelDisplayName( modelName )
	{
		var normalized = String( modelName || "player" ).replace( /\\/g, "/" );
		var slash = normalized.lastIndexOf( "/" );
		var name = slash >= 0 ? normalized.substring( slash + 1 ) : normalized;
		name = name.replace( /\.mdl$/i, "" ).replace( /[^A-Za-z0-9_.-]+/g, "_" );
		return name || "player";
	}

	function agrTrackForHandle( tracksByHandle, orderedTracks, handle, modelName )
	{
		var key = String( handle );
		var track = tracksByHandle[key];
		if ( !track )
		{
			track = {
				handle: handle,
				modelName: modelName,
				samples: [],
				lastFrame: -2,
				hidden: true
			};
			tracksByHandle[key] = track;
			orderedTracks.push( track );
		}
		else if ( !track.modelName )
			track.modelName = modelName;
		return track;
	}

	function agrMarkHandleHidden( tracksByHandle, handle )
	{
		var track = tracksByHandle[String( handle )];
		if ( track )
			track.hidden = true;
	}

	/*
	 * HLAE AGR player-position parsing is based on AdvancedFX's official
	 * advancedfx_import_gameRecord.py importer. AGR is a binary dictionary-
	 * packet stream; versions 5 and 6 store entity root transforms as vectors /
	 * angles and 3x4 matrices respectively. Only player root positions are kept.
	 * Source: https://github.com/advancedfx/afx-sfm-scripts/
	 */
	function parseAgrPlayerPositions( file )
	{
		if ( !file || !file.exists )
			throw new Error( "The selected AGR file does not exist." );
		file.encoding = "BINARY";
		if ( !file.open( "r" ) )
			throw new Error( "Could not open " + file.fsName );

		var tracksByHandle = {};
		var orderedTracks = [];
		var frameIndex = -1;
		var version = 0;
		try
		{
			var reader = new AgrBinaryReader( file );
			var magic = reader.readBytes( 14 );
			if ( magic !== "afxGameRecord\x00" )
				throw new Error( file.name + " is not an AdvancedFX AGR file." );
			version = reader.readInt32();
			if ( version !== 5 && version !== 6 )
				throw new Error( file.name + " uses unsupported AGR version " + version +
					". This importer supports versions 5 and 6." );

			var dictionary = new AgrDictionary( reader );
			while ( true )
			{
				var packet = dictionary.read();
				if ( packet === null )
					break;

				if ( packet === "afxFrame" )
				{
					reader.readFloat32(); // Recorded frame duration; ART take FPS is authoritative.
					reader.readInt32();   // Offset to the hidden list; parsed normally below.
					++frameIndex;
				}
				else if ( packet === "afxFrameEnd" )
				{
					// Packet boundary only.
				}
				else if ( packet === "afxHidden" )
				{
					var hiddenCount = reader.readInt32();
					if ( hiddenCount === null || hiddenCount < 0 )
						throw new Error( "AGR hidden-entity list is invalid." );
					for ( var hiddenIndex = 0; hiddenIndex < hiddenCount; ++hiddenIndex )
						agrMarkHandleHidden( tracksByHandle, reader.readInt32() );
				}
				else if ( packet === "deleted" )
				{
					agrMarkHandleHidden( tracksByHandle, reader.readInt32() );
				}
				else if ( packet === "entity_state" )
				{
					var handle = reader.readInt32();
					var modelName = null;
					var visible = false;
					var sourcePosition = null;

					if ( dictionary.peek( "baseentity" ) )
					{
						modelName = dictionary.read();
						visible = reader.readBool();
						if ( version === 6 )
							sourcePosition = reader.readMatrix3x4Position();
						else
						{
							sourcePosition = reader.readVector();
							reader.skipFloats( 3 ); // QAngle.
						}
					}

					if ( dictionary.peek( "baseanimating" ) )
					{
						var hasBoneList = reader.readBool();
						if ( hasBoneList )
						{
							var boneCount = reader.readInt32();
							if ( boneCount === null || boneCount < 0 )
								throw new Error( "AGR bone count is invalid." );
							for ( var boneIndex = 0; boneIndex < boneCount; ++boneIndex )
								reader.skipFloats( version === 6 ? 12 : 7 );
						}
					}

					if ( dictionary.peek( "camera" ) )
					{
						reader.readBool();
						reader.skipFloats( 3 ); // Position.
						reader.skipFloats( 3 ); // Angles.
						reader.readFloat32();   // FOV.
					}

					if ( !dictionary.peek( "/" ) )
						throw new Error( "AGR entity_state terminator is missing." );
					var viewModel = reader.readBool();

					if ( modelName && agrIsPlayerModel( modelName ) && !viewModel )
					{
						var track = agrTrackForHandle(
							tracksByHandle, orderedTracks, handle, modelName );
						if ( visible && sourcePosition && isFinite( sourcePosition[0] ) &&
							isFinite( sourcePosition[1] ) && isFinite( sourcePosition[2] ) &&
							frameIndex >= 0 )
						{
							var breakBefore = track.samples.length > 0 &&
								( track.hidden || track.lastFrame + 1 !== frameIndex );
							var sample = {
								frame: frameIndex,
								position: sourcePosition,
								breakBefore: breakBefore
							};
							if ( track.samples.length &&
								track.samples[track.samples.length - 1].frame === frameIndex )
								track.samples[track.samples.length - 1] = sample;
							else
								track.samples.push( sample );
							track.lastFrame = frameIndex;
							track.hidden = false;
						}
						else
							track.hidden = true;
					}
					else
						agrMarkHandleHidden( tracksByHandle, handle );
				}
				else if ( packet === "afxCam" )
				{
					reader.skipFloats( 3 ); // Position.
					reader.skipFloats( 3 ); // Angles.
					reader.readFloat32();   // FOV.
				}
				else
					throw new Error( "Unknown AGR packet '" + packet + "'." );
			}
		}
		finally
		{
			file.close();
		}

		var nonEmptyTracks = [];
		for ( var i = 0; i < orderedTracks.length; ++i )
		{
			if ( orderedTracks[i].samples.length )
				nonEmptyTracks.push( orderedTracks[i] );
		}
		return {
			version: version,
			frameCount: Math.max( 0, frameIndex + 1 ),
			tracks: nonEmptyTracks
		};
	}

	function setAgrGapInterpolation( property, samples )
	{
		if ( !property )
			return;
		for ( var i = 1; i < samples.length; ++i )
		{
			if ( !samples[i].breakBefore && samples[i].frame === samples[i - 1].frame + 1 )
				continue;
			try
			{
				property.setInterpolationTypeAtKey( i,
					KeyframeInterpolationType.LINEAR, KeyframeInterpolationType.HOLD );
			}
			catch ( ignoredPreviousGapInterpolation ) {}
			try
			{
				property.setInterpolationTypeAtKey( i + 1,
					KeyframeInterpolationType.HOLD, KeyframeInterpolationType.LINEAR );
			}
			catch ( ignoredNextGapInterpolation ) {}
		}
	}

	function addAgrPlayerNullsToComp( comp, parsedAgr, frameRate, frameCount, warnings )
	{
		if ( !comp || !parsedAgr || !parsedAgr.tracks )
			return 0;
		var expectedFrames = Math.max( 1, Math.round( Number( frameCount ) || 1 ) );
		var rate = Number( frameRate );
		if ( !isFinite( rate ) || rate <= 0 )
			rate = 25;
		var created = 0;

		for ( var trackIndex = 0; trackIndex < parsedAgr.tracks.length; ++trackIndex )
		{
			var track = parsedAgr.tracks[trackIndex];
			var usableSamples = [];
			for ( var sampleIndex = 0; sampleIndex < track.samples.length; ++sampleIndex )
			{
				if ( track.samples[sampleIndex].frame < expectedFrames )
					usableSamples.push( track.samples[sampleIndex] );
			}
			if ( !usableSamples.length )
				continue;

			var layer = comp.layers.addNull( comp.duration );
			layer.threeDLayer = true;
			layer.name = "AGR Player " + ( created + 1 ) + " - " +
				agrModelDisplayName( track.modelName ) + " [" + track.handle + "]";
			try { layer.autoOrient = AutoOrientType.NO_AUTO_ORIENT; }
			catch ( ignoredAgrAutoOrientError ) {}

			var transform = layer.property( "ADBE Transform Group" );
			var position = transform ? transform.property( "ADBE Position" ) : null;
			if ( !position )
			{
				try { layer.remove(); }
				catch ( ignoredAgrNullRemoveError ) {}
				warnings.push( "Could not resolve Position for AGR player handle " +
					track.handle + "." );
				continue;
			}

			var times = [];
			var positions = [];
			for ( var keyIndex = 0; keyIndex < usableSamples.length; ++keyIndex )
			{
				var sample = usableSamples[keyIndex];
				times.push( sample.frame / rate );
				// Raw Source / AGR world coordinates use the CamIO mapping:
				// Source X -> AE Z, Source Y -> AE -X, Source Z -> AE -Y.
				positions.push( [ -sample.position[1], -sample.position[2],
					sample.position[0] ] );
			}

			try
			{
				setValuesAtTimesSafe( position, times, positions );
				setLinearTemporalInterpolation( position );
				setAgrGapInterpolation( position, usableSamples );
				finishCameraLayerRange( layer, comp );
				++created;
			}
			catch ( nullKeyError )
			{
				try { layer.remove(); }
				catch ( ignoredFailedAgrNullRemoveError ) {}
				warnings.push( "Could not write AGR player keys for handle " +
					track.handle + ": " + nullKeyError.toString() );
			}
		}

		if ( parsedAgr.frameCount > expectedFrames )
		{
			warnings.push( "AGR contains " + parsedAgr.frameCount + " frames for a " +
				expectedFrames + "-frame take; trailing player samples were ignored." );
		}
		else if ( parsedAgr.frameCount < expectedFrames )
		{
			warnings.push( "AGR contains " + parsedAgr.frameCount + " frames for a " +
				expectedFrames + "-frame take; player nulls stop at the last AGR sample." );
		}
		return created;
	}

	function importAgrPlayerNulls( takeFolder, manifest, masterComp, settings,
		frameRate, frameCount, warnings )
	{
		if ( !settings.importAgrPlayers )
			return 0;
		if ( !masterComp )
		{
			warnings.push( "HLAE AGR player import requires the layered master composition." );
			return 0;
		}
		var agrFile = findAgrFile( takeFolder, manifest.take.name );
		if ( !agrFile )
		{
			warnings.push( "No HLAE .agr file was found in the take or AGR / HLAE folder." );
			return 0;
		}
		try
		{
			var parsedAgr = parseAgrPlayerPositions( agrFile );
			var created = addAgrPlayerNullsToComp(
				masterComp, parsedAgr, frameRate, frameCount, warnings );
			if ( !created )
				warnings.push( "The AGR file contained no usable models/player positions." );
			return created;
		}
		catch ( agrError )
		{
			warnings.push( "HLAE AGR player import failed: " + agrError.toString() );
			return 0;
		}
	}


	AgrBinaryReader.prototype.readQAngle = function()
	{
		return [ this.readFloat32(), this.readFloat32(), this.readFloat32() ];
	};

	AgrBinaryReader.prototype.readQuaternion = function()
	{
		return [ this.readFloat32(), this.readFloat32(),
			this.readFloat32(), this.readFloat32() ];
	};

	AgrBinaryReader.prototype.readMatrix3x4 = function()
	{
		var matrix = [];
		for ( var i = 0; i < 12; ++i )
			matrix.push( this.readFloat32() );
		return matrix;
	};

	function agrFiniteVector( value, expectedLength )
	{
		if ( !value || value.length < expectedLength )
			return false;
		for ( var i = 0; i < expectedLength; ++i )
		{
			if ( !isFinite( Number( value[i] ) ) )
				return false;
		}
		return true;
	}

	function agrMatrixPosition( matrix )
	{
		return [ matrix[3], matrix[7], matrix[11] ];
	}

	function agrNormalizeMatrixColumns( matrix )
	{
		var result = [
			[ matrix[0], matrix[1], matrix[2] ],
			[ matrix[4], matrix[5], matrix[6] ],
			[ matrix[8], matrix[9], matrix[10] ]
		];
		for ( var column = 0; column < 3; ++column )
		{
			var length = Math.sqrt(
				result[0][column] * result[0][column] +
				result[1][column] * result[1][column] +
				result[2][column] * result[2][column] );
			if ( isFinite( length ) && length > 0.000001 )
			{
				result[0][column] /= length;
				result[1][column] /= length;
				result[2][column] /= length;
			}
		}
		return result;
	}

	function agrSourceMatrixToAngles( matrix )
	{
		var rotation = agrNormalizeMatrixColumns( matrix );
		var pitchRadians = Math.asin( Math.max( -1, Math.min( 1, -rotation[2][0] ) ) );
		var cosinePitch = Math.cos( pitchRadians );
		var yawRadians = 0;
		var rollRadians = 0;
		if ( Math.abs( cosinePitch ) > 0.000001 )
		{
			yawRadians = Math.atan2( rotation[1][0], rotation[0][0] );
			rollRadians = Math.atan2( rotation[2][1], rotation[2][2] );
		}
		else
		{
			yawRadians = Math.atan2( -rotation[0][1], rotation[1][1] );
			rollRadians = 0;
		}
		return [ radiansToDegrees( pitchRadians ),
			radiansToDegrees( yawRadians ), radiansToDegrees( rollRadians ) ];
	}

	function agrQuaternionToMatrix( quaternion )
	{
		var x = Number( quaternion[0] );
		var y = Number( quaternion[1] );
		var z = Number( quaternion[2] );
		var w = Number( quaternion[3] );
		var length = Math.sqrt( x * x + y * y + z * z + w * w );
		if ( !isFinite( length ) || length <= 0.000001 )
			return [ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 ];
		x /= length;
		y /= length;
		z /= length;
		w /= length;
		return [
			1 - 2 * ( y * y + z * z ), 2 * ( x * y - z * w ),
			2 * ( x * z + y * w ), 0,
			2 * ( x * y + z * w ), 1 - 2 * ( x * x + z * z ),
			2 * ( y * z - x * w ), 0,
			2 * ( x * z - y * w ), 2 * ( y * z + x * w ),
			1 - 2 * ( x * x + y * y ), 0
		];
	}

	function agrSourceAnglesToAeOrientation( angles )
	{
		if ( !agrFiniteVector( angles, 3 ) )
			return [ 0, 0, 0 ];
		// Source QAngle order is pitch, yaw, roll. Reuse the credited CamIO
		// HPB conversion so AGR roots and cameras align with imported footage.
		return camioOrientationToAe( {
			roll: Number( angles[2] ),
			pitch: Number( angles[0] ),
			yaw: Number( angles[1] )
		} );
	}

	function agrMatrixToAeOrientation( matrix )
	{
		return agrSourceAnglesToAeOrientation( agrSourceMatrixToAngles( matrix ) );
	}

	function agrQuaternionToAeOrientation( quaternion )
	{
		return agrMatrixToAeOrientation( agrQuaternionToMatrix( quaternion ) );
	}

	function agrNearestAngle( previous, current )
	{
		var value = Number( current );
		var reference = Number( previous );
		if ( !isFinite( value ) || !isFinite( reference ) )
			return value;
		while ( value - reference > 180 ) value -= 360;
		while ( value - reference < -180 ) value += 360;
		return value;
	}

	function agrUnwrapOrientation( previous, current )
	{
		if ( !previous )
			return current;
		return [
			agrNearestAngle( previous[0], current[0] ),
			agrNearestAngle( previous[1], current[1] ),
			agrNearestAngle( previous[2], current[2] )
		];
	}

	function agrFullTrackForPacket( currentByHandle, orderedTracks,
		handle, modelName, viewModel )
	{
		var key = String( handle );
		var track = currentByHandle[key];
		if ( !track || track.modelName !== modelName || track.viewModel !== viewModel )
		{
			track = {
				handle: handle,
				modelName: modelName || "unknown_model",
				viewModel: !!viewModel,
				samples: [],
				bones: {},
				boneOrder: [],
				cameraSamples: [],
				lastFrame: -2,
				hidden: true,
				lastOrientation: null,
				lastCameraOrientation: null
			};
			currentByHandle[key] = track;
			orderedTracks.push( track );
		}
		return track;
	}


	function agrBoneTrackForIndex( entityTrack, boneIndex )
	{
		var key = String( boneIndex );
		var boneTrack = entityTrack.bones[key];
		if ( !boneTrack )
		{
			boneTrack = {
				index: boneIndex,
				samples: [],
				lastFrame: -2,
				lastOrientation: null
			};
			entityTrack.bones[key] = boneTrack;
			entityTrack.boneOrder.push( boneTrack );
		}
		return boneTrack;
	}

	function agrMarkFullHandleHidden( currentByHandle, handle, removeHandle )
	{
		var key = String( handle );
		var track = currentByHandle[key];
		if ( track )
			track.hidden = true;
		if ( removeHandle )
			delete currentByHandle[key];
	}

	function agrAppendTransformSample( target, frameIndex, sourcePosition,
		aeOrientation, hidden, lastFrameName, lastOrientationName )
	{
		if ( frameIndex < 0 || !agrFiniteVector( sourcePosition, 3 ) ||
			!agrFiniteVector( aeOrientation, 3 ) )
			return;
		var lastFrame = Number( target[lastFrameName] );
		var breakBefore = target.samples && target.samples.length > 0 &&
			( hidden || lastFrame + 1 !== frameIndex );
		var orientation = agrUnwrapOrientation(
			target[lastOrientationName], aeOrientation );
		var sample = {
			frame: frameIndex,
			position: sourcePosition,
			orientation: orientation,
			breakBefore: breakBefore
		};
		return sample;
	}

	/*
	 * Full AGR mode imports every baseentity root transform, including players,
	 * weapons, props and viewmodels. Entity-attached camera packets and the
	 * global afxCam packet are imported as separate null tracks. Bone payloads
	 * are emitted as independent indexed nulls. They are intentionally not
	 * parented because AGR does not contain bone names or skeleton hierarchy.
	 */
	function parseAgrFullTransforms( file, includeBones, includeAfxCam )
	{
		if ( !file || !file.exists )
			throw new Error( "The selected AGR file does not exist." );
		file.encoding = "BINARY";
		if ( !file.open( "r" ) )
			throw new Error( "Could not open " + file.fsName );

		var currentByHandle = {};
		var orderedTracks = [];
		var globalCamera = {
			samples: [], lastFrame: -2, lastOrientation: null
		};
		var frameIndex = -1;
		var version = 0;
		try
		{
			var reader = new AgrBinaryReader( file );
			var magic = reader.readBytes( 14 );
			if ( magic !== "afxGameRecord\x00" )
				throw new Error( file.name + " is not an AdvancedFX AGR file." );
			version = reader.readInt32();
			if ( version !== 5 && version !== 6 )
				throw new Error( file.name + " uses unsupported AGR version " + version +
					". This importer supports versions 5 and 6." );

			var dictionary = new AgrDictionary( reader );
			while ( true )
			{
				var packet = dictionary.read();
				if ( packet === null )
					break;

				if ( packet === "afxFrame" )
				{
					reader.readFloat32();
					reader.readInt32();
					++frameIndex;
				}
				else if ( packet === "afxFrameEnd" )
				{
					// Packet boundary only.
				}
				else if ( packet === "afxHidden" )
				{
					var hiddenCount = reader.readInt32();
					if ( hiddenCount === null || hiddenCount < 0 )
						throw new Error( "AGR hidden-entity list is invalid." );
					for ( var hiddenIndex = 0; hiddenIndex < hiddenCount; ++hiddenIndex )
						agrMarkFullHandleHidden(
							currentByHandle, reader.readInt32(), false );
				}
				else if ( packet === "deleted" )
				{
					agrMarkFullHandleHidden( currentByHandle, reader.readInt32(), true );
				}
				else if ( packet === "entity_state" )
				{
					var handle = reader.readInt32();
					var modelName = null;
					var visible = false;
					var sourcePosition = null;
					var aeOrientation = null;
					var boneTransforms = [];
					var embeddedCamera = null;

					if ( dictionary.peek( "baseentity" ) )
					{
						modelName = dictionary.read();
						visible = reader.readBool();
						if ( version === 6 )
						{
							var rootMatrix = reader.readMatrix3x4();
							sourcePosition = agrMatrixPosition( rootMatrix );
							aeOrientation = agrMatrixToAeOrientation( rootMatrix );
						}
						else
						{
							sourcePosition = reader.readVector();
							aeOrientation = agrSourceAnglesToAeOrientation(
								reader.readQAngle() );
						}
					}

					if ( dictionary.peek( "baseanimating" ) )
					{
						var hasBoneList = reader.readBool();
						if ( hasBoneList )
						{
							var boneCount = reader.readInt32();
							if ( boneCount === null || boneCount < 0 )
								throw new Error( "AGR bone count is invalid." );
							for ( var boneIndex = 0; boneIndex < boneCount; ++boneIndex )
							{
								var bonePosition = null;
								var boneOrientation = null;
								if ( version === 6 )
								{
									var boneMatrix = reader.readMatrix3x4();
									bonePosition = agrMatrixPosition( boneMatrix );
									boneOrientation = agrMatrixToAeOrientation( boneMatrix );
								}
								else
								{
									bonePosition = reader.readVector();
									boneOrientation = agrQuaternionToAeOrientation(
										reader.readQuaternion() );
								}
								if ( includeBones )
								{
									boneTransforms.push( {
										index: boneIndex,
										position: bonePosition,
										orientation: boneOrientation
									} );
								}
							}
						}
					}

					if ( dictionary.peek( "camera" ) )
					{
						embeddedCamera = {
							thirdPerson: reader.readBool(),
							position: reader.readVector(),
							orientation: agrSourceAnglesToAeOrientation(
								reader.readQAngle() ),
							fov: reader.readFloat32()
						};
					}

					if ( !dictionary.peek( "/" ) )
						throw new Error( "AGR entity_state terminator is missing." );
					var viewModel = reader.readBool();

					if ( modelName )
					{
						var track = agrFullTrackForPacket(
							currentByHandle, orderedTracks, handle, modelName, viewModel );
						if ( visible && sourcePosition && aeOrientation )
						{
							var rootSample = agrAppendTransformSample(
								track, frameIndex, sourcePosition, aeOrientation,
								track.hidden, "lastFrame", "lastOrientation" );
							if ( rootSample )
							{
								if ( track.samples.length &&
									track.samples[track.samples.length - 1].frame === frameIndex )
									track.samples[track.samples.length - 1] = rootSample;
								else
									track.samples.push( rootSample );
								track.lastFrame = frameIndex;
								track.lastOrientation = rootSample.orientation;
								track.hidden = false;
							}
						}
						else
							track.hidden = true;

						if ( visible && boneTransforms.length )
						{
							for ( var storedBoneIndex = 0;
								storedBoneIndex < boneTransforms.length; ++storedBoneIndex )
							{
								var boneTransform = boneTransforms[storedBoneIndex];
								var boneTrack = agrBoneTrackForIndex(
									track, boneTransform.index );
								var boneSample = agrAppendTransformSample(
									boneTrack, frameIndex, boneTransform.position,
									boneTransform.orientation, track.hidden,
									"lastFrame", "lastOrientation" );
								if ( !boneSample )
									continue;
								if ( boneTrack.samples.length &&
									boneTrack.samples[boneTrack.samples.length - 1].frame === frameIndex )
									boneTrack.samples[boneTrack.samples.length - 1] = boneSample;
								else
									boneTrack.samples.push( boneSample );
								boneTrack.lastFrame = frameIndex;
								boneTrack.lastOrientation = boneSample.orientation;
							}
						}

						if ( embeddedCamera &&
							agrFiniteVector( embeddedCamera.position, 3 ) )
						{
							var cameraOrientation = agrUnwrapOrientation(
								track.lastCameraOrientation, embeddedCamera.orientation );
							track.cameraSamples.push( {
								frame: frameIndex,
								position: embeddedCamera.position,
								orientation: cameraOrientation,
								fov: embeddedCamera.fov,
								thirdPerson: embeddedCamera.thirdPerson,
								breakBefore: track.cameraSamples.length > 0 &&
									track.cameraSamples[track.cameraSamples.length - 1].frame + 1 !== frameIndex
							} );
							track.lastCameraOrientation = cameraOrientation;
						}
					}
				}
				else if ( packet === "afxCam" )
				{
					var globalPosition = reader.readVector();
					var globalOrientation = agrSourceAnglesToAeOrientation(
						reader.readQAngle() );
					var globalFov = reader.readFloat32();
					if ( includeAfxCam && frameIndex >= 0 &&
						agrFiniteVector( globalPosition, 3 ) )
					{
						globalOrientation = agrUnwrapOrientation(
							globalCamera.lastOrientation, globalOrientation );
						globalCamera.samples.push( {
							frame: frameIndex,
							position: globalPosition,
							orientation: globalOrientation,
							fov: globalFov,
							breakBefore: globalCamera.samples.length > 0 &&
								globalCamera.lastFrame + 1 !== frameIndex
						} );
						globalCamera.lastFrame = frameIndex;
						globalCamera.lastOrientation = globalOrientation;
					}
				}
				else
					throw new Error( "Unknown AGR packet '" + packet + "'." );
			}
		}
		finally
		{
			file.close();
		}

		var nonEmptyTracks = [];
		for ( var i = 0; i < orderedTracks.length; ++i )
		{
			if ( orderedTracks[i].samples.length || orderedTracks[i].boneOrder.length ||
				orderedTracks[i].cameraSamples.length )
				nonEmptyTracks.push( orderedTracks[i] );
		}
		return {
			version: version,
			frameCount: Math.max( 0, frameIndex + 1 ),
			tracks: nonEmptyTracks,
			globalCameraSamples: globalCamera.samples
		};
	}

	function agrSourcePositionToAe( position )
	{
		return [ -position[1], -position[2], position[0] ];
	}

	function addAgrFovControl( layer, samples, frameRate )
	{
		if ( !layer || !samples || !samples.length )
			return;
		var effects = layer.property( "ADBE Effect Parade" );
		if ( !effects )
			return;
		var effect = null;
		try { effect = effects.addProperty( "ADBE Slider Control" ); }
		catch ( ignoredAgrFovEffectError ) {}
		if ( !effect )
			return;
		effect.name = "AGR FOV";
		var slider = effect.property( 1 );
		if ( !slider )
			return;
		var times = [];
		var values = [];
		for ( var i = 0; i < samples.length; ++i )
		{
			if ( isFinite( Number( samples[i].fov ) ) )
			{
				times.push( samples[i].frame / frameRate );
				values.push( Number( samples[i].fov ) );
			}
		}
		if ( times.length )
		{
			setValuesAtTimesSafe( slider, times, values );
			setLinearTemporalInterpolation( slider );
		}
	}

	function addAgrTransformNullToComp( comp, name, samples, frameRate,
		frameCount, warnings, comment, addFov )
	{
		if ( !samples || !samples.length )
			return null;
		var expectedFrames = Math.max( 1, Math.round( Number( frameCount ) || 1 ) );
		var usableSamples = [];
		for ( var i = 0; i < samples.length; ++i )
		{
			if ( samples[i].frame >= 0 && samples[i].frame < expectedFrames )
				usableSamples.push( samples[i] );
		}
		if ( !usableSamples.length )
			return null;

		var layer = comp.layers.addNull( comp.duration );
		layer.threeDLayer = true;
		layer.name = name;
		try { layer.autoOrient = AutoOrientType.NO_AUTO_ORIENT; }
		catch ( ignoredAgrFullAutoOrientError ) {}
		try { layer.comment = comment || ""; }
		catch ( ignoredAgrCommentError ) {}

		var transform = layer.property( "ADBE Transform Group" );
		var position = transform ? transform.property( "ADBE Position" ) : null;
		var orientation = transform ? transform.property( "ADBE Orientation" ) : null;
		if ( !position || !orientation )
		{
			try { layer.remove(); }
			catch ( ignoredAgrFullRemoveError ) {}
			warnings.push( "Could not resolve Position / Orientation for " + name + "." );
			return null;
		}

		var times = [];
		var positions = [];
		var orientations = [];
		for ( var keyIndex = 0; keyIndex < usableSamples.length; ++keyIndex )
		{
			var sample = usableSamples[keyIndex];
			times.push( sample.frame / frameRate );
			positions.push( agrSourcePositionToAe( sample.position ) );
			orientations.push( sample.orientation );
		}

		try
		{
			setValuesAtTimesSafe( position, times, positions );
			setValuesAtTimesSafe( orientation, times, orientations );
			setLinearTemporalInterpolation( position );
			setLinearTemporalInterpolation( orientation );
			setAgrGapInterpolation( position, usableSamples );
			setAgrGapInterpolation( orientation, usableSamples );
			if ( addFov )
				addAgrFovControl( layer, usableSamples, frameRate );
			finishCameraLayerRange( layer, comp );
			return layer;
		}
		catch ( keyError )
		{
			try { layer.remove(); }
			catch ( ignoredFailedAgrFullRemoveError ) {}
			warnings.push( "Could not write full AGR keys for " + name +
				": " + keyError.toString() );
			return null;
		}
	}

	function addAgrFullNullsToComp( comp, parsedAgr, frameRate,
		frameCount, warnings, includeBones, includeAfxCam )
	{
		if ( !comp || !parsedAgr )
			return 0;
		var rate = Number( frameRate );
		if ( !isFinite( rate ) || rate <= 0 )
			rate = 25;
		var created = 0;
		var entityNumber = 0;
		var cameraNumber = 0;

		for ( var trackIndex = 0; trackIndex < parsedAgr.tracks.length; ++trackIndex )
		{
			var track = parsedAgr.tracks[trackIndex];
			if ( track.samples.length )
			{
				++entityNumber;
				var entityName = "AGR " + ( track.viewModel ? "Viewmodel" : "Entity" ) +
					" " + entityNumber + " - " + agrModelDisplayName( track.modelName ) +
					" [" + track.handle + "]";
				if ( addAgrTransformNullToComp( comp, entityName, track.samples,
					rate, frameCount, warnings,
					"AGR model: " + track.modelName + "\nHandle: " + track.handle +
					"\nViewmodel: " + ( track.viewModel ? "yes" : "no" ), false ) )
					++created;
			}

			if ( includeBones )
			{
				for ( var boneTrackIndex = 0;
					boneTrackIndex < track.boneOrder.length; ++boneTrackIndex )
				{
					var boneTrack = track.boneOrder[boneTrackIndex];
					if ( !boneTrack.samples.length )
						continue;
					var paddedBoneIndex = String( boneTrack.index );
					while ( paddedBoneIndex.length < 3 )
						paddedBoneIndex = "0" + paddedBoneIndex;
					var boneName = "AGR Bone " + paddedBoneIndex + " - " +
						agrModelDisplayName( track.modelName ) + " [" + track.handle + "]";
					if ( addAgrTransformNullToComp( comp, boneName, boneTrack.samples,
						rate, frameCount, warnings,
						"Raw AGR bone transform\nModel: " + track.modelName +
						"\nHandle: " + track.handle + "\nBone index: " + boneTrack.index +
						"\nAGR does not include bone names or hierarchy.", false ) )
						++created;
				}
			}

			if ( track.cameraSamples.length )
			{
				++cameraNumber;
				var attachedCameraName = "AGR Entity Camera " + cameraNumber +
					" - " + agrModelDisplayName( track.modelName ) + " [" +
					track.handle + "]";
				if ( addAgrTransformNullToComp( comp, attachedCameraName,
					track.cameraSamples, rate, frameCount, warnings,
					"AGR entity-attached camera\nModel: " + track.modelName +
					"\nHandle: " + track.handle, true ) )
					++created;
			}
		}

		if ( includeAfxCam && parsedAgr.globalCameraSamples &&
			parsedAgr.globalCameraSamples.length )
		{
			if ( addAgrTransformNullToComp( comp, "AGR Camera - afxCam",
				parsedAgr.globalCameraSamples, rate, frameCount, warnings,
				"Global AGR afxCam track", true ) )
				++created;
		}

		var expectedFrames = Math.max( 1, Math.round( Number( frameCount ) || 1 ) );
		if ( parsedAgr.frameCount > expectedFrames )
		{
			warnings.push( "AGR contains " + parsedAgr.frameCount + " frames for a " +
				expectedFrames + "-frame take; trailing full-AGR samples were ignored." );
		}
		else if ( parsedAgr.frameCount < expectedFrames )
		{
			warnings.push( "AGR contains " + parsedAgr.frameCount + " frames for a " +
				expectedFrames + "-frame take; full-AGR nulls stop at the last AGR sample." );
		}
		return created;
	}

	function importAgrFullNulls( takeFolder, manifest, masterComp, settings,
		frameRate, frameCount, warnings )
	{
		if ( !settings.importAgrFull )
			return 0;
		if ( !masterComp )
		{
			warnings.push( "Full HLAE AGR import requires the layered master composition." );
			return 0;
		}
		var agrFile = findAgrFile( takeFolder, manifest.take.name );
		if ( !agrFile )
		{
			warnings.push( "No HLAE .agr file was found in the take or AGR / HLAE folder." );
			return 0;
		}
		try
		{
			var includeBones = settings.importAgrBones !== false;
			var includeAfxCam = settings.importAgrAfxCam !== false;
			var parsedAgr = parseAgrFullTransforms(
				agrFile, includeBones, includeAfxCam );
			var created = addAgrFullNullsToComp(
				masterComp, parsedAgr, frameRate, frameCount, warnings,
				includeBones, includeAfxCam );
			if ( !created )
				warnings.push( "The AGR file contained no usable entity or camera transforms." );
			return created;
		}
		catch ( agrError )
		{
			warnings.push( "Full HLAE AGR import failed: " + agrError.toString() );
			return 0;
		}
	}

	function normalizedChannelName( value )
	{
		return String( value ).replace( /[^A-Za-z0-9]/g, "" ).toLowerCase();
	}

	function channelIndexMap( channels )
	{
		var result = {};
		for ( var i = 0; i < channels.length; ++i )
			result[normalizedChannelName( channels[i] )] = i;
		return result;
	}

	function requiredChannelIndex( map, name, file )
	{
		var normalized = normalizedChannelName( name );
		if ( typeof map[normalized] === "undefined" )
			throw new Error( file.name + " is missing the " + name + " channel." );
		return map[normalized];
	}

	/*
	 * mirv_camio implementation adapted from HLAE CamIO To AE v2.0 by
	 * Brett Anthony / xNWP. The original implementation uses a one-camera
	 * Orientation workflow, Source-to-AE position mapping [-Y, -Z, X], an
	 * HPB rotation matrix decomposition, and CamIO scaleFov metadata.
	 * Source: https://github.com/xNWP/HLAE-CamIO-To-AE
	 */
	function parseCamioCamera( file )
	{
		var lines = readTextFile( file ).split( /\r?\n/ );
		var magicLine = -1;
		for ( var lineIndex = 0; lineIndex < lines.length; ++lineIndex )
		{
			var possibleMagic = String( lines[lineIndex] )
				.replace( /^\uFEFF/, "" ).replace( /^\s+|\s+$/g, "" );
			if ( possibleMagic )
			{
				magicLine = lineIndex;
				if ( possibleMagic.toLowerCase() !== "advancedfx cam" )
					throw new Error( file.name + " is not an AdvancedFX CAM file." );
				break;
			}
		}
		if ( magicLine < 0 )
			throw new Error( file.name + " is empty." );

		var version = -1;
		var scaleFov = true;
		var channels = [
			"time", "xPosition", "yPosition", "zPosition",
			"xRotation", "yRotation", "zRotation", "fov"
		];
		var dataLine = -1;
		for ( var headerIndex = magicLine + 1; headerIndex < lines.length; ++headerIndex )
		{
			var header = splitWords( lines[headerIndex] );
			if ( !header.length )
				continue;
			var command = String( header[0] ).toLowerCase();
			if ( command === "data" )
			{
				dataLine = headerIndex + 1;
				break;
			}
			if ( command === "version" && header.length > 1 )
				version = Number( header[1] );
			else if ( command === "scalefov" && header.length > 1 )
				scaleFov = String( header[1] ).toLowerCase() !== "none";
			else if ( command === "channels" && header.length > 1 )
			{
				channels = [];
				for ( var channelIndex = 1; channelIndex < header.length; ++channelIndex )
					channels.push( header[channelIndex] );
			}
		}
		if ( dataLine < 0 )
			throw new Error( file.name + " has no DATA section." );
		if ( !isFinite( version ) || version < 1 || version > 2 )
			throw new Error( file.name + " uses unsupported CAM version " + version + "." );

		var map = channelIndexMap( channels );
		var timeIndex = requiredChannelIndex( map, "time", file );
		var xIndex = requiredChannelIndex( map, "xPosition", file );
		var yIndex = requiredChannelIndex( map, "yPosition", file );
		var zIndex = requiredChannelIndex( map, "zPosition", file );
		var xRotationIndex = requiredChannelIndex( map, "xRotation", file );
		var yRotationIndex = requiredChannelIndex( map, "yRotation", file );
		var zRotationIndex = requiredChannelIndex( map, "zRotation", file );
		var fovIndex = requiredChannelIndex( map, "fov", file );
		var minimumValues = Math.max( timeIndex, xIndex, yIndex, zIndex,
			xRotationIndex, yRotationIndex, zRotationIndex, fovIndex ) + 1;
		var samples = [];
		for ( var sampleLine = dataLine; sampleLine < lines.length; ++sampleLine )
		{
			var values = splitWords( lines[sampleLine] );
			if ( values.length < minimumValues )
				continue;
			var sample = {
				time: Number( values[timeIndex] ),
				x: Number( values[xIndex] ),
				y: Number( values[yIndex] ),
				z: Number( values[zIndex] ),
				roll: Number( values[xRotationIndex] ),
				pitch: Number( values[yRotationIndex] ),
				yaw: Number( values[zRotationIndex] ),
				fov: Number( values[fovIndex] )
			};
			if ( isFinite( sample.time ) && isFinite( sample.x ) &&
				isFinite( sample.y ) && isFinite( sample.z ) &&
				isFinite( sample.roll ) && isFinite( sample.pitch ) &&
				isFinite( sample.yaw ) && isFinite( sample.fov ) )
				samples.push( sample );
		}
		if ( !samples.length )
			throw new Error( file.name + " contains no usable camera samples." );
		return { samples: samples, scaleFov: scaleFov, version: version };
	}

	/*
	 * mirv_camexport implementation adapted from “HLAE BVH to AE Cam” v1.5
	 * by msthavoc (advancedfx.org). The original script drives translation
	 * and heading on a Y null, pitch / bank on an XZ null, and parents the
	 * AE camera under that two-null rig.
	 */
	function parseCamexportCamera( file )
	{
		var lines = readTextFile( file ).split( /\r?\n/ );
		var motionLine = -1;
		var channels = null;
		for ( var hierarchyIndex = 0; hierarchyIndex < lines.length; ++hierarchyIndex )
		{
			var hierarchyLine = String( lines[hierarchyIndex] )
				.replace( /^\uFEFF/, "" ).replace( /^\s+|\s+$/g, "" );
			if ( hierarchyLine.toUpperCase() === "MOTION" )
			{
				motionLine = hierarchyIndex;
				break;
			}
			var hierarchyWords = splitWords( hierarchyLine );
			if ( !channels && hierarchyWords.length > 2 &&
				String( hierarchyWords[0] ).toUpperCase() === "CHANNELS" )
			{
				var channelCount = Number( hierarchyWords[1] );
				if ( !isFinite( channelCount ) || channelCount <= 0 ||
					hierarchyWords.length < channelCount + 2 )
					throw new Error( file.name + " has an invalid BVH CHANNELS declaration." );
				channels = [];
				for ( var channelIndex = 0; channelIndex < channelCount; ++channelIndex )
					channels.push( hierarchyWords[channelIndex + 2] );
			}
		}
		if ( motionLine < 0 || !channels )
			throw new Error( file.name + " is missing BVH hierarchy or MOTION data." );

		var map = channelIndexMap( channels );
		var xPositionIndex = requiredChannelIndex( map, "Xposition", file );
		var yPositionIndex = requiredChannelIndex( map, "Yposition", file );
		var zPositionIndex = requiredChannelIndex( map, "Zposition", file );
		var zRotationIndex = requiredChannelIndex( map, "Zrotation", file );
		var xRotationIndex = requiredChannelIndex( map, "Xrotation", file );
		var yRotationIndex = requiredChannelIndex( map, "Yrotation", file );
		var expectedValues = channels.length;
		var frameTime = 0;
		var reportedFrames = 0;
		var dataLine = -1;
		for ( var motionIndex = motionLine + 1; motionIndex < lines.length; ++motionIndex )
		{
			var motionText = String( lines[motionIndex] ).replace( /^\s+|\s+$/g, "" );
			var framesMatch = motionText.match( /^Frames\s*:\s*(\d+)/i );
			if ( framesMatch )
			{
				reportedFrames = Number( framesMatch[1] );
				continue;
			}
			var timeMatch = motionText.match( /^Frame\s+Time\s*:\s*([^\s]+)/i );
			if ( timeMatch )
			{
				frameTime = Number( timeMatch[1] );
				dataLine = motionIndex + 1;
				break;
			}
		}
		if ( !isFinite( frameTime ) || frameTime <= 0 || dataLine < 0 )
			throw new Error( file.name + " has no valid BVH Frame Time." );

		var samples = [];
		for ( var sampleLine = dataLine; sampleLine < lines.length; ++sampleLine )
		{
			if ( reportedFrames > 0 && samples.length >= reportedFrames )
				break;
			var words = splitWords( lines[sampleLine] );
			if ( words.length < expectedValues )
				continue;
			var values = [];
			var valuesValid = true;
			for ( var valueIndex = 0; valueIndex < expectedValues; ++valueIndex )
			{
				values[valueIndex] = Number( words[valueIndex] );
				if ( !isFinite( values[valueIndex] ) )
					valuesValid = false;
			}
			if ( !valuesValid )
				continue;

			// msthavoc mapping for HLAE's usual root order:
			// position [X, -Y, -Z], XZ.X = Xrotation,
			// Y.Y = -Yrotation, XZ.Z = -Zrotation.
			samples.push( {
				time: samples.length * frameTime,
				x: values[xPositionIndex],
				y: values[yPositionIndex],
				z: values[zPositionIndex],
				b: values[zRotationIndex],
				h: values[xRotationIndex],
				p: values[yRotationIndex]
			} );
		}
		if ( !samples.length )
			throw new Error( file.name + " contains no usable BVH camera samples." );
		return { samples: samples, frameTime: frameTime, reportedFrames: reportedFrames };
	}

	function degreesToRadians( angle )
	{
		return Number( angle ) * Math.PI / 180;
	}

	function radiansToDegrees( angle )
	{
		return Number( angle ) * 180 / Math.PI;
	}

	function multiplyRotationMatrices( matrix, otherMatrix )
	{
		var result = [ [ 1, 0, 0 ], [ 0, 1, 0 ], [ 0, 0, 1 ] ];
		for ( var row = 0; row < 3; ++row )
		{
			for ( var column = 0; column < 3; ++column )
			{
				result[row][column] = 0;
				for ( var index = 0; index < 3; ++index )
					result[row][column] += matrix[row][index] * otherMatrix[index][column];
			}
		}
		return result;
	}

	function rotationMatrixX( angle )
	{
		var cosine = Math.cos( angle );
		var sine = Math.sin( angle );
		return [ [ 1, 0, 0 ], [ 0, cosine, -sine ], [ 0, sine, cosine ] ];
	}

	function rotationMatrixY( angle )
	{
		var cosine = Math.cos( angle );
		var sine = Math.sin( angle );
		return [ [ cosine, 0, sine ], [ 0, 1, 0 ], [ -sine, 0, cosine ] ];
	}

	function rotationMatrixZ( angle )
	{
		var cosine = Math.cos( angle );
		var sine = Math.sin( angle );
		return [ [ cosine, -sine, 0 ], [ sine, cosine, 0 ], [ 0, 0, 1 ] ];
	}

	function camioOrientationToAe( sample )
	{
		var xRotation = degreesToRadians( sample.roll );
		var yRotation = degreesToRadians( sample.pitch );
		var zRotation = degreesToRadians( sample.yaw );

		// xNWP: build Source HPB intrinsic rotation, then decompose into the
		// PHB order used by After Effects Orientation.
		var rotation = rotationMatrixX( xRotation );
		rotation = multiplyRotationMatrices( rotationMatrixY( yRotation ), rotation );
		rotation = multiplyRotationMatrices( rotationMatrixZ( zRotation ), rotation );

		yRotation = radiansToDegrees( Math.atan2( -rotation[2][0], rotation[0][0] ) );
		xRotation = radiansToDegrees( Math.atan2( -rotation[1][2], rotation[1][1] ) );
		var r31 = rotation[2][0];
		var r11 = rotation[0][0];
		zRotation = radiansToDegrees(
			Math.atan2( rotation[1][0], Math.sqrt( r31 * r31 + r11 * r11 ) ) );
		return [ -yRotation, -zRotation, xRotation ];
	}

	function cameraZoomForHorizontalFov( width, fov )
	{
		var safeFov = Number( fov );
		if ( !isFinite( safeFov ) || safeFov <= 0 || safeFov >= 180 )
			safeFov = 90;
		return Number( width ) / ( 2 * Math.tan( degreesToRadians( safeFov / 2 ) ) );
	}

	function camioZoomForFov( comp, fov, scaleFov )
	{
		var adjustedFov = Number( fov );
		if ( !isFinite( adjustedFov ) || adjustedFov <= 0 || adjustedFov >= 180 )
			adjustedFov = 90;

		// xNWP's original scaleFov=none conversion relates the recorded 4:3
		// horizontal FOV to the target composition aspect ratio.
		if ( !scaleFov )
		{
			var relatedRatio = ( comp.width / comp.height ) / ( 4.0 / 3.0 );
			adjustedFov = adjustedFov / 2;
			adjustedFov = degreesToRadians( adjustedFov );
			adjustedFov = Math.tan( adjustedFov );
			adjustedFov = radiansToDegrees( adjustedFov ) * relatedRatio;
			adjustedFov = Math.atan( degreesToRadians( adjustedFov ) );
			adjustedFov = radiansToDegrees( adjustedFov ) * 2;
		}
		return cameraZoomForHorizontalFov( comp.width, adjustedFov );
	}

	function engineFovFromManifest( manifest, warnings )
	{
		// mirv_camexport BVH has no FOV channel. ART records the effective
		// Source-engine horizontal camera FOV in the take JSON.
		var candidates = [];
		try { candidates.push( manifest.camera.source_engine_actual.camera_horizontal_degrees ); }
		catch ( ignoredActualHorizontalFov ) {}
		try { candidates.push( manifest.camera.source_engine_actual.fov ); }
		catch ( ignoredActualFov ) {}
		try { candidates.push( manifest.camera.engine_fov ); }
		catch ( ignoredCameraEngineFov ) {}
		try { candidates.push( manifest.capture.engine_fov ); }
		catch ( ignoredCaptureEngineFov ) {}
		try { candidates.push( manifest.engine_fov ); }
		catch ( ignoredRootEngineFov ) {}

		for ( var i = 0; i < candidates.length; ++i )
		{
			var value = Number( candidates[i] );
			if ( isFinite( value ) && value > 0 && value < 180 )
				return value;
		}

		warnings.push(
			"The take JSON has no valid engine camera FOV; mirv_camexport BVH uses 90 degrees." );
		return 90;
	}

	function cameraSamplesOnTakeTimeline( samples, frameRate, frameCount,
		cameraName, warnings )
	{
		// Both credited source scripts ultimately key by composition frame index.
		// ART's take FPS and frame count are therefore authoritative; source file
		// timestamps never resize or offset the imported composition.
		var rate = Number( frameRate );
		if ( !isFinite( rate ) || rate <= 0 )
			rate = 25;
		var expectedFrames = Math.max( 1, Math.round( Number( frameCount ) || 1 ) );
		var usableCount = Math.min( samples.length, expectedFrames );
		var result = [];
		for ( var i = 0; i < usableCount; ++i )
		{
			var source = samples[i];
			result.push( {
				time: i / rate,
				x: Number( source.x ), y: Number( source.y ), z: Number( source.z ),
				roll: Number( source.roll ), pitch: Number( source.pitch ),
				yaw: Number( source.yaw ), fov: Number( source.fov ),
				b: Number( source.b ), h: Number( source.h ), p: Number( source.p )
			} );
		}
		if ( samples.length > expectedFrames )
		{
			warnings.push( cameraName + " contains " + samples.length +
				" samples for a " + expectedFrames + "-frame take; " +
				( samples.length - expectedFrames ) +
				" trailing camera sample(s) were ignored to preserve exact take duration." );
		}
		else if ( samples.length < expectedFrames )
		{
			warnings.push( cameraName + " contains " + samples.length +
				" samples for a " + expectedFrames + "-frame take; " +
				"the last camera value will hold through the remaining take frames." );
		}
		return result;
	}

	function setValuesAtTimesSafe( property, times, values )
	{
		try
		{
			property.setValuesAtTimes( times, values );
		}
		catch ( batchKeyError )
		{
			for ( var i = 0; i < times.length; ++i )
				property.setValueAtTime( times[i], values[i] );
		}
	}

	function setLinearTemporalInterpolation( property )
	{
		if ( !property )
			return;
		for ( var keyIndex = 1; keyIndex <= property.numKeys; ++keyIndex )
		{
			try
			{
				property.setInterpolationTypeAtKey( keyIndex,
					KeyframeInterpolationType.LINEAR, KeyframeInterpolationType.LINEAR );
			}
			catch ( ignoredInterpolationError ) {}
			try { property.setSpatialAutoBezierAtKey( keyIndex, false ); }
			catch ( ignoredSpatialAutoBezierError ) {}
			try { property.setSpatialContinuousAtKey( keyIndex, false ); }
			catch ( ignoredSpatialContinuousError ) {}
		}
	}

	function finishCameraLayerRange( layer, comp )
	{
		try
		{
			layer.inPoint = 0;
			layer.outPoint = comp.duration;
		}
		catch ( ignoredLayerRangeError ) {}
	}

	function addCamioCameraToComp( comp, parsedCamio, name, frameRate,
		frameCount, warnings )
	{
		if ( !comp || !parsedCamio || !parsedCamio.samples || !parsedCamio.samples.length )
			return null;
		var samples = cameraSamplesOnTakeTimeline(
			parsedCamio.samples, frameRate, frameCount, name, warnings );
		if ( !samples.length )
			return null;

		var camera = comp.layers.addCamera( name, [ 0, 0 ] );
		camera.name = name;
		try { camera.autoOrient = AutoOrientType.NO_AUTO_ORIENT; }
		catch ( ignoredAutoOrientError ) {}

		var transform = camera.property( "ADBE Transform Group" );
		var position = transform ? transform.property( "ADBE Position" ) : null;
		var orientation = transform ? transform.property( "ADBE Orientation" ) : null;
		var options = camera.property( "ADBE Camera Options Group" );
		var zoom = options ? options.property( "ADBE Camera Zoom" ) : null;
		if ( !position || !orientation || !zoom )
		{
			try { camera.remove(); }
			catch ( ignoredCameraRemoveError ) {}
			throw new Error( "Could not resolve Position, Orientation or Zoom for " + name + "." );
		}

		var times = [];
		var positions = [];
		var orientations = [];
		var zooms = [];
		for ( var i = 0; i < samples.length; ++i )
		{
			var sample = samples[i];
			times.push( sample.time );
			// xNWP Source-to-AE mapping: X -> Z, Y -> -X, Z -> -Y.
			positions.push( [ -sample.y, -sample.z, sample.x ] );
			orientations.push( camioOrientationToAe( sample ) );
			zooms.push( camioZoomForFov( comp, sample.fov, parsedCamio.scaleFov ) );
		}

		try
		{
			setValuesAtTimesSafe( position, times, positions );
			setValuesAtTimesSafe( orientation, times, orientations );
			setValuesAtTimesSafe( zoom, times, zooms );
			setLinearTemporalInterpolation( position );
			setLinearTemporalInterpolation( orientation );
			setLinearTemporalInterpolation( zoom );
		}
		catch ( keyError )
		{
			try { camera.remove(); }
			catch ( ignoredFailedCameraRemoveError ) {}
			throw new Error( "Could not write mirv_camio camera keys: " + keyError.toString() );
		}
		finishCameraLayerRange( camera, comp );
		return camera;
	}

	function addBvhCameraRigToComp( comp, parsedBvh, name, frameRate,
		frameCount, warnings, engineFov )
	{
		if ( !comp || !parsedBvh || !parsedBvh.samples || !parsedBvh.samples.length )
			return null;
		var samples = cameraSamplesOnTakeTimeline(
			parsedBvh.samples, frameRate, frameCount, name, warnings );
		if ( !samples.length )
			return null;

		var camera = null;
		var xzNull = null;
		var yNull = null;
		try
		{
			camera = comp.layers.addCamera( name, [ 0, 0 ] );
			camera.name = name;
			camera.autoOrient = AutoOrientType.NO_AUTO_ORIENT;
			var cameraTransform = camera.property( "ADBE Transform Group" );
			var cameraPosition = cameraTransform ?
				cameraTransform.property( "ADBE Position" ) : null;
			if ( cameraPosition )
				cameraPosition.setValue( [ 0, 0, 0 ] );

			xzNull = comp.layers.addNull();
			xzNull.threeDLayer = true;
			xzNull.name = "ART BVH XZ - mirv_camexport";
			yNull = comp.layers.addNull();
			yNull.threeDLayer = true;
			yNull.name = "ART BVH Y - mirv_camexport";

			camera.parent = xzNull;
			xzNull.parent = yNull;

			var xzTransform = xzNull.property( "ADBE Transform Group" );
			var yTransform = yNull.property( "ADBE Transform Group" );
			var xzPosition = xzTransform ? xzTransform.property( "ADBE Position" ) : null;
			var yPosition = yTransform ? yTransform.property( "ADBE Position" ) : null;
			var yRotation = yTransform ? yTransform.property( "ADBE Rotate Y" ) : null;
			var xRotation = xzTransform ? xzTransform.property( "ADBE Rotate X" ) : null;
			var zRotation = xzTransform ? xzTransform.property( "ADBE Rotate Z" ) : null;
			var cameraOptions = camera.property( "ADBE Camera Options Group" );
			var zoom = cameraOptions ? cameraOptions.property( "ADBE Camera Zoom" ) : null;
			if ( !xzPosition || !yPosition || !yRotation || !xRotation || !zRotation || !zoom )
				throw new Error( "Could not resolve the msthavoc BVH camera rig properties." );

			// The credited msthavoc rig explicitly places both nulls at the origin.
			// addNull() otherwise starts at the composition center and offsets the camera.
			xzPosition.setValue( [ 0, 0, 0 ] );
			yPosition.setValue( [ 0, 0, 0 ] );

			var times = [];
			var positions = [];
			var xRotations = [];
			var yRotations = [];
			var zRotations = [];
			for ( var i = 0; i < samples.length; ++i )
			{
				var sample = samples[i];
				times.push( sample.time );
				positions.push( [ sample.x, -sample.y, -sample.z ] );
				xRotations.push( sample.h );
				yRotations.push( -sample.p );
				zRotations.push( -sample.b );
			}

			setValuesAtTimesSafe( yPosition, times, positions );
			setValuesAtTimesSafe( yRotation, times, yRotations );
			setValuesAtTimesSafe( xRotation, times, xRotations );
			setValuesAtTimesSafe( zRotation, times, zRotations );
			zoom.setValue( cameraZoomForHorizontalFov( comp.width, engineFov ) );

			setLinearTemporalInterpolation( yPosition );
			setLinearTemporalInterpolation( yRotation );
			setLinearTemporalInterpolation( xRotation );
			setLinearTemporalInterpolation( zRotation );
			finishCameraLayerRange( camera, comp );
			finishCameraLayerRange( xzNull, comp );
			finishCameraLayerRange( yNull, comp );
			return camera;
		}
		catch ( rigError )
		{
			try { if ( camera ) camera.remove(); }
			catch ( ignoredCameraRemoveError ) {}
			try { if ( xzNull ) xzNull.remove(); }
			catch ( ignoredXzRemoveError ) {}
			try { if ( yNull ) yNull.remove(); }
			catch ( ignoredYRemoveError ) {}
			throw new Error( "Could not create mirv_camexport BVH rig: " + rigError.toString() );
		}
	}

	function importHlaeCameras( takeFolder, manifest, masterComp, settings,
		frameRate, frameCount, warnings )
	{
		if ( !masterComp )
		{
			if ( settings.importCamioCamera || settings.importCamexportCamera )
				warnings.push( "HLAE camera import requires the layered master composition." );
			return 0;
		}
		var imported = 0;

		if ( settings.importCamioCamera )
		{
			var camFile = findCameraFile( takeFolder, "cam" );
			if ( camFile )
			{
				try
				{
					var camioCamera = addCamioCameraToComp(
						masterComp, parseCamioCamera( camFile ),
						"ART Camera - mirv_camio", frameRate, frameCount, warnings );
					if ( camioCamera )
						++imported;
				}
				catch ( camError )
				{
					warnings.push( "mirv_camio camera import failed: " + camError.toString() );
				}
			}
			else
				warnings.push( "No mirv_camio .cam file was found in the take or camera folder." );
		}
		if ( settings.importCamexportCamera )
		{
			var bvhFile = findCameraFile( takeFolder, "bvh" );
			if ( bvhFile )
			{
				try
				{
					var actualFov = engineFovFromManifest( manifest, warnings );
					var bvhCamera = addBvhCameraRigToComp(
						masterComp, parseCamexportCamera( bvhFile ),
						"ART Camera - mirv_camexport", frameRate, frameCount,
						warnings, actualFov );
					if ( bvhCamera )
						++imported;
				}
				catch ( bvhError )
				{
					warnings.push( "mirv_camexport camera import failed: " +
						bvhError.toString() );
				}
			}
			else
				warnings.push( "No mirv_camexport .bvh file was found in the take or camera folder." );
		}
		return imported;
	}

	function addEffectByCandidates( layer, candidates )
	{
		var effects = layer.property( "ADBE Effect Parade" );
		if ( !effects )
			return null;
		for ( var i = 0; i < candidates.length; ++i )
		{
			try
			{
				if ( !effects.canAddProperty || effects.canAddProperty( candidates[i] ) )
					return effects.addProperty( candidates[i] );
			}
			catch ( ignoredEffectName ) {}
		}
		return null;
	}

	function setFirstColorProperty( group, rgb )
	{
		if ( !group )
			return false;
		for ( var i = 1; i <= group.numProperties; ++i )
		{
			var property = group.property( i );
			if ( !property )
				continue;
			try
			{
				if ( property.propertyValueType === PropertyValueType.COLOR )
				{
					var color = [ rgb[0] / 255, rgb[1] / 255, rgb[2] / 255 ];
					try { property.setValue( color ); }
					catch ( threeChannelError )
					{
						property.setValue( [ color[0], color[1], color[2], 1 ] );
					}
					return true;
				}
				if ( property.numProperties && setFirstColorProperty( property, rgb ) )
					return true;
			}
			catch ( ignoredColorProperty ) {}
		}
		return false;
	}

	function findNamedProperty( group, fragments )
	{
		if ( !group )
			return null;
		for ( var i = 1; i <= group.numProperties; ++i )
		{
			var property = group.property( i );
			if ( !property )
				continue;
			var name = "";
			try { name = String( property.name ).toLowerCase(); }
			catch ( ignoredName ) {}
			for ( var j = 0; j < fragments.length; ++j )
			{
				if ( name.indexOf( fragments[j] ) >= 0 )
					return property;
			}
			try
			{
				if ( property.numProperties )
				{
					var nested = findNamedProperty( property, fragments );
					if ( nested )
						return nested;
				}
			}
			catch ( ignoredNestedProperty ) {}
		}
		return null;
	}

	function findExactProperty( group, matchNames, names )
	{
		if ( !group )
			return null;
		for ( var i = 1; i <= group.numProperties; ++i )
		{
			var property = group.property( i );
			if ( !property )
				continue;
			var matchName = "";
			var name = "";
			try { matchName = String( property.matchName ); }
			catch ( ignoredMatchName ) {}
			try { name = String( property.name ).toLowerCase(); }
			catch ( ignoredExactName ) {}
			for ( var j = 0; j < matchNames.length; ++j )
			{
				if ( matchName === matchNames[j] )
					return property;
			}
			for ( var k = 0; k < names.length; ++k )
			{
				if ( name === names[k] )
					return property;
			}
		}
		return null;
	}

	function applyAutomaticKey( layer, mode, rgb, objectIdMatte, warnings )
	{
		if ( mode === "none" || !rgb )
			return;
		var effect = null;
		if ( mode === "linear" )
		{
			effect = addEffectByCandidates(
				layer, [ "ADBE Linear Color Key2", "ADBE Linear Color Key", "Linear Color Key" ] );
			if ( effect && objectIdMatte )
			{
				var operation = findExactProperty(
					effect, [ "ADBE Linear Color Key2-0007" ], [ "key operation" ] );
				if ( !operation )
				{
					try { operation = effect.property( 7 ); }
					catch ( ignoredOperation ) {}
				}
				try
				{
					if ( operation ) operation.setValue( 1 );
					else warnings.push( "ObjectID Linear Color Key has no Key Operation property." );
				}
				catch ( keyOperationError )
				{
					warnings.push( "Could not set ObjectID Key Operation to Key Colors." );
				}
				var view = findExactProperty(
					effect, [ "ADBE Linear Color Key2-0002" ], [ "view" ] );
				if ( !view )
				{
					try { view = effect.property( 2 ); }
					catch ( ignoredView ) {}
				}
				try
				{
					if ( view ) view.setValue( 3 );
					else warnings.push( "ObjectID Linear Color Key has no View property." );
				}
				catch ( matteViewError )
				{
					warnings.push( "Could not set the ObjectID key View to Matte Only." );
				}
			}
			var tolerance = effect ?
				findExactProperty( effect, [ "ADBE Linear Color Key2-0005" ],
					[ "matching tolerance" ] ) : null;
			var softness = effect ?
				findExactProperty( effect, [ "ADBE Linear Color Key2-0006" ],
					[ "matching softness" ] ) : null;
			try { if ( tolerance ) tolerance.setValue( 10 ); }
			catch ( ignoredTolerance ) {}
			try { if ( softness ) softness.setValue( 2 ); }
			catch ( ignoredSoftness ) {}
		}
		else if ( mode === "keylight" )
		{
			effect = addEffectByCandidates(
				layer, [ "Keylight 1.2", "Keylight (1.2)", "Keylight" ] );
		}
		if ( !effect )
		{
			warnings.push( "Could not add " +
				( mode === "linear" ? "Linear Color Key" : "Keylight (1.2)" ) +
				" to " + layer.name + "." );
			return;
		}
		if ( !setFirstColorProperty( effect, rgb ) )
			warnings.push( "The key effect was added to " + layer.name +
				", but its screen-color property was not found." );
		if ( mode === "linear" && objectIdMatte )
		{
			var invert = addEffectByCandidates( layer, [ "ADBE Invert", "Invert" ] );
			if ( !invert )
				warnings.push( "Could not add Invert after the ObjectID Linear Color Key on " +
					layer.name + "." );
		}
	}

	function findPass( manifest, name )
	{
		for ( var i = 0; i < manifest.passes.length; ++i )
		{
			if ( manifest.passes[i].name === name )
				return manifest.passes[i];
		}
		return null;
	}

	function createObjectIdMattes( matteFolder, takeName, footageItem, pass,
		width, height, duration, frameRate, warnings )
	{
		if ( !pass.category_colors )
		{
			warnings.push( "ObjectID separation was requested, but category colors are absent from the manifest." );
			return 0;
		}
		var names = [ "viewmodel", "players", "world", "skybox" ];
		var created = 0;
		for ( var i = 0; i < names.length; ++i )
		{
			var category = names[i];
			var rgb = pass.category_colors[category];
			if ( !rgb )
				continue;
			var displayName = category.charAt( 0 ).toUpperCase() + category.substring( 1 );
			var matteComp = app.project.items.addComp(
				takeName + " - ObjectID - " + displayName + " Matte",
				width, height, 1, duration, frameRate );
			matteComp.parentFolder = matteFolder;
			var layer = matteComp.layers.add( footageItem );
			layer.name = displayName + " ID";
			applyAutomaticKey( layer, "linear", rgb, true, warnings );
			++created;
		}
		return created;
	}


	function createObjectIdMattesFromExrLayer( matteFolder, takeName,
		sourceLayer, pass, width, height, duration, frameRate, warnings )
	{
		if ( !sourceLayer || !pass || !pass.category_colors )
		{
			warnings.push( "ObjectID separation was requested, but the configured " +
				"ObjectID EXtractoR layer or category colors are unavailable." );
			return 0;
		}

		var names = [ "viewmodel", "players", "world", "skybox" ];
		var created = 0;
		for ( var i = 0; i < names.length; ++i )
		{
			var category = names[i];
			var rgb = pass.category_colors[category];
			if ( !rgb )
				continue;
			var displayName = category.charAt( 0 ).toUpperCase() +
				category.substring( 1 );
			var matteComp = app.project.items.addComp(
				takeName + " - ObjectID - " + displayName + " Matte",
				width, height, 1, duration, frameRate );
			matteComp.parentFolder = matteFolder;

			var layer = null;
			try
			{
				sourceLayer.copyToComp( matteComp );
				layer = matteComp.layer( 1 );
			}
			catch ( copyObjectIdLayerError )
			{
				warnings.push( "Could not copy the ObjectID EXtractoR layer for " +
					displayName + ": " + copyObjectIdLayerError.toString() );
				try { matteComp.remove(); }
				catch ( ignoredFailedObjectIdCompRemoveError ) {}
				continue;
			}

			layer.name = displayName + " ID";
			layer.enabled = true;
			try
			{
				layer.inPoint = 0;
				layer.outPoint = duration;
			}
			catch ( ignoredObjectIdExrRangeError ) {}
			applyAutomaticKey( layer, "linear", rgb, true, warnings );
			++created;
		}
		return created;
	}


	function exrLayerSearchName( layer )
	{
		var names = [];
		try { names.push( String( layer.name ) ); }
		catch ( ignoredExrLayerNameError ) {}
		var source = null;
		try { source = layer.source; }
		catch ( ignoredExrLayerSourceError ) {}
		if ( source )
		{
			try { names.push( String( source.name ) ); }
			catch ( ignoredExrSourceNameError ) {}
			if ( source instanceof CompItem )
			{
				for ( var i = 1; i <= source.numLayers; ++i )
				{
					var nestedLayer = source.layer( i );
					// EXtractoR's Channel Info is a custom plug-in control and is not
					// consistently enumerable through ExtendScript. Names generated by
					// AE's layered importer are therefore the authoritative pass signal.
					try { names.push( String( nestedLayer.name ) ); }
					catch ( ignoredNestedExrLayerNameError ) {}
					try
					{
						if ( nestedLayer.source )
							names.push( String( nestedLayer.source.name ) );
					}
					catch ( ignoredNestedExrSourceNameError ) {}
				}
			}
		}
		return names.join( " " );
	}

	function passForExrLayer( manifest, layer )
	{
		return passForExrText( manifest, exrLayerSearchName( layer ) );
	}

	function indexExrPassLayers( masterComp, manifest, settings, warnings )
	{
		var imported = {};
		if ( !masterComp )
			return imported;
		for ( var i = 1; i <= masterComp.numLayers; ++i )
		{
			var layer = masterComp.layer( i );
			var layerName = "";
			try { layerName = String( layer.name ).toLowerCase(); }
			catch ( ignoredExrIndexLayerNameError ) {}
			if ( layerName.indexOf( "extractor assignment failed" ) >= 0 )
				continue;
			var pass = passForExrLayer( manifest, layer );
			if ( !pass || imported[pass.name] )
				continue;
			try { layer.name = pass.name; }
			catch ( ignoredExrPassRenameError ) {}
			imported[pass.name] = { item: layer.source, pass: pass, layer: layer };
			if ( pass.name === "objectid" || pass.name === "depth" )
				layer.enabled = false;
			if ( pass.name === "viewmodel" || pass.name === "players" )
				applyAutomaticKey( layer, settings.keyMode,
					pass.background_rgb, false, warnings );
		}
		return imported;
	}


	function createEditingPrecomp( compositionFolder, masterComp, takeName,
		requestedFrameRate, warnings )
	{
		if ( !masterComp )
			return null;
		var editRate = Number( requestedFrameRate );
		if ( !isFinite( editRate ) || editRate <= 0 || editRate > 999 )
		{
			warnings.push( "Editing precomp FPS is invalid; 25 FPS was used." );
			editRate = 25;
		}
		var editComp = app.project.items.addComp(
			takeName + " - EDIT " + editRate + "fps",
			masterComp.width, masterComp.height, masterComp.pixelAspect,
			masterComp.duration, editRate );
		editComp.parentFolder = compositionFolder;
		var masterLayer = editComp.layers.add( masterComp );
		masterLayer.name = masterComp.name;
		try
		{
			masterLayer.startTime = 0;
			masterLayer.inPoint = 0;
			masterLayer.outPoint = editComp.duration;
		}
		catch ( ignoredEditLayerRangeError ) {}
		try { masterComp.preserveNestedFrameRate = true; }
		catch ( ignoredPreserveNestedRateError ) {}
		try
		{
			editComp.workAreaStart = 0;
			editComp.workAreaDuration = editComp.duration;
		}
		catch ( ignoredEditWorkAreaError ) {}
		return editComp;
	}

	function importTake( settings )
	{
		var manifestFile = new File( settings.manifestPath );
		var manifest = parseJson( readTextFile( manifestFile ) );
		validateManifest( manifest );
		var takeFolder = resolveTakeFolder( manifestFile, manifest );
		var frameRate = normalizedFrameRate( manifest, settings.fallbackFrameRate );
		var width = Number( manifest.capture.width ) || 1920;
		var height = Number( manifest.capture.height ) || 1080;
		var frames = Number( manifest.capture.frames ) || 1;
		var duration = Math.max( 1 / frameRate, frames / frameRate );
		var warnings = [];


		var rootFolder = app.project.items.addFolder( "ART - " + manifest.take.name );
		var compositionFolder = null;
		if ( settings.createComposition )
		{
			compositionFolder = app.project.items.addFolder( "01 - Compositions" );
			compositionFolder.parentFolder = rootFolder;
		}
		var sourceCompFolder = null;
		if ( settings.mediaMode === "exr" && settings.createComposition )
		{
			sourceCompFolder = app.project.items.addFolder( "02 - EXR Source Comps" );
			sourceCompFolder.parentFolder = rootFolder;
		}
		var footageFolder = app.project.items.addFolder( "04 - Footage" );
		footageFolder.parentFolder = rootFolder;
		var imported = {};
		var importedCount = 0;
		var masterComp = null;
		var exrExpanded = false;
		var extractorLayerCount = 0;

		if ( settings.mediaMode === "exr" )
		{
			var exrResult = importMultilayerExr(
				takeFolder, compositionFolder, sourceCompFolder, footageFolder,
				manifest.take.name,
				width, height, duration, frameRate,
				settings.createComposition, manifest, warnings );
			masterComp = exrResult.masterComp;
			exrExpanded = exrResult.expanded;
			extractorLayerCount = exrResult.extractorLayerCount || 0;
			importedCount = 1;
			if ( masterComp && exrExpanded )
			{
				imported = indexExrPassLayers( masterComp, manifest, settings, warnings );
				var matchedExrPassCount = 0;
				for ( var matchedPassName in imported )
				{
					if ( imported.hasOwnProperty && !imported.hasOwnProperty( matchedPassName ) )
						continue;
					++matchedExrPassCount;
				}
				if ( matchedExrPassCount === 0 )
					warnings.push( "EXtractoR layers were created, but none matched the pass " +
						"names stored in the ART take manifest." );
			}
		}
		else
		{
			for ( var i = 0; i < manifest.passes.length; ++i )
			{
				var pass = manifest.passes[i];
				if ( Number( pass.files ) <= 0 )
					continue;
				var mediaFile = settings.mediaMode === "sequence" ?
					findSequenceFile( takeFolder, pass ) :
					findVideoFile( takeFolder, pass, settings.videoExtension );
				if ( !mediaFile )
				{
					warnings.push( "No " +
						( settings.mediaMode === "sequence" ? "sequence" : "video" ) +
						" found for pass '" + pass.name + "'." );
					continue;
				}
				try
				{
					var footage = importMedia(
						mediaFile, settings.mediaMode === "sequence", frameRate );
					footage.name = manifest.take.name + " - " + pass.name;
					footage.parentFolder = footageFolder;
					imported[pass.name] = { item: footage, pass: pass };
					++importedCount;
					if ( settings.mediaMode === "sequence" &&
						pass.frame_ranges && pass.frame_ranges.length > 1 )
					{
						warnings.push( "Pass '" + pass.name +
							"' contains multiple frame ranges; verify intentional gaps after import." );
					}
				}
				catch ( importError )
				{
					warnings.push( "Could not import pass '" + pass.name +
						"': " + importError.toString() );
				}
			}

			if ( importedCount === 0 )
				throw new Error( "No pass media could be imported. Check the selected footage mode and take paths." );

			if ( settings.createComposition )
			{
				masterComp = app.project.items.addComp(
					manifest.take.name + " - ART",
					width, height, 1, duration, frameRate );
				masterComp.parentFolder = compositionFolder;
				var layerOrder = [
					"objectid", "depth", "normal", "clear",
					"clear-noplayers", "players", "viewmodel"
				];
				for ( var order = 0; order < layerOrder.length; ++order )
				{
					var importedPass = imported[layerOrder[order]];
					if ( !importedPass )
						continue;
					var layer = masterComp.layers.add( importedPass.item );
					layer.name = importedPass.pass.name;
					if ( importedPass.pass.name === "objectid" )
						layer.enabled = false;
					else if ( importedPass.pass.name === "depth" )
					{
						var depthLayerIndex = layer.index;
						try
						{
							var depthComp = masterComp.layers.precompose(
								[ depthLayerIndex ],
								manifest.take.name + " - Depth Precomp", true );
							depthComp.parentFolder = compositionFolder;
							layer = masterComp.layer( depthLayerIndex );
							layer.name = "depth (precomposed)";
						}
						catch ( depthPrecomposeError )
						{
							warnings.push( "Depth could not be pre-composed: " +
								depthPrecomposeError.toString() );
						}
						layer.enabled = false;
					}
					if ( importedPass.pass.name === "viewmodel" ||
						importedPass.pass.name === "players" )
					{
						applyAutomaticKey( layer, settings.keyMode,
							importedPass.pass.background_rgb, false, warnings );
					}
				}
			}
		}

		var separatedCount = 0;
		if ( settings.separateObjectId && imported.objectid )
		{
			var matteFolder = app.project.items.addFolder( "03 - ObjectID Mattes" );
			matteFolder.parentFolder = rootFolder;
			if ( settings.mediaMode === "exr" && imported.objectid.layer )
			{
				separatedCount = createObjectIdMattesFromExrLayer(
					matteFolder, manifest.take.name, imported.objectid.layer,
					imported.objectid.pass, width, height, duration, frameRate, warnings );
			}
			else
			{
				separatedCount = createObjectIdMattes(
					matteFolder, manifest.take.name, imported.objectid.item,
					imported.objectid.pass, width, height, duration, frameRate, warnings );
			}
		}
		else if ( settings.separateObjectId && settings.mediaMode === "exr" && !exrExpanded )
		{
			warnings.push( "ObjectID matte separation requires successfully assigned " +
				"ObjectID EXtractoR channels." );
		}

		var cameraCount = importHlaeCameras(
			takeFolder, manifest, masterComp, settings, frameRate, frames, warnings );
		var playerNullCount = 0;
		var fullAgrNullCount = 0;
		if ( settings.importAgrFull )
		{
			fullAgrNullCount = importAgrFullNulls(
				takeFolder, manifest, masterComp, settings, frameRate, frames, warnings );
		}
		else
		{
			playerNullCount = importAgrPlayerNulls(
				takeFolder, manifest, masterComp, settings, frameRate, frames, warnings );
		}

		if ( masterComp )
		{
			masterComp.duration = duration;
			try
			{
				masterComp.workAreaStart = 0;
				masterComp.workAreaDuration = duration;
			}
			catch ( ignoredWorkAreaError ) {}
		}

		var editingComp = null;
		if ( settings.createEditingPrecomp && masterComp )
		{
			editingComp = createEditingPrecomp(
				compositionFolder, masterComp, manifest.take.name,
				settings.editingFrameRate, warnings );
		}

		return {
			manifest: manifest,
			mediaMode: settings.mediaMode,
			importedCount: importedCount,
			separatedCount: separatedCount,
			cameraCount: cameraCount,
			playerNullCount: playerNullCount,
			fullAgrNullCount: fullAgrNullCount,
			masterComp: masterComp,
			editingComp: editingComp,
			exrExpanded: exrExpanded,
			extractorLayerCount: extractorLayerCount,
			warnings: warnings
		};
	}

	function buildUi( owner )
	{
		var window = owner instanceof Panel ? owner :
			new Window( "palette", SCRIPT_NAME, undefined, { resizeable: true } );
		window.orientation = "column";
		window.alignChildren = [ "fill", "top" ];
		window.spacing = 8;
		window.margins = 12;

		var manifestPanel = window.add( "panel", undefined, "ART take manifest" );
		manifestPanel.orientation = "row";
		manifestPanel.alignChildren = [ "fill", "center" ];
		manifestPanel.margins = 10;
		var manifestPath = manifestPanel.add( "edittext", undefined, "" );
		manifestPath.characters = 52;
		var browse = manifestPanel.add( "button", undefined, "Browse..." );

		var importPanel = window.add( "panel", undefined, "Import options" );
		importPanel.orientation = "column";
		importPanel.alignChildren = [ "left", "center" ];
		importPanel.margins = 10;

		var modeRow = importPanel.add( "group" );
		modeRow.add( "statictext", undefined, "Footage source:" );
		var mode = modeRow.add( "dropdownlist", undefined,
			[ "TGA image sequences", "Converted video files",
				"Multilayer OpenEXR sequence (<take>/EXR)" ] );
		mode.selection = 0;

		var videoRow = importPanel.add( "group" );
		videoRow.add( "statictext", undefined, "Video extension:" );
		var videoExtension = videoRow.add( "dropdownlist", undefined,
			[ "Auto", ".mov", ".mp4", ".avi", ".mxf", ".mpg" ] );
		videoExtension.selection = 0;
		videoRow.enabled = false;


		var rateRow = importPanel.add( "group" );
		rateRow.add( "statictext", undefined, "Fallback FPS when manifest is real-time:" );
		var fallbackRate = rateRow.add( "edittext", undefined, "25" );
		fallbackRate.characters = 7;

		var keyRow = importPanel.add( "group" );
		keyRow.add( "statictext", undefined, "Auto-key Viewmodel / Players:" );
		var keyMode = keyRow.add( "dropdownlist", undefined,
			[ "Keylight (1.2)", "Linear Color Key", "None" ] );
		keyMode.selection = 0;

		var createComposition = importPanel.add(
			"checkbox", undefined, "Create layered master composition" );
		createComposition.value = true;
		var createEditingPrecomp = importPanel.add(
			"checkbox", undefined, "Create editing precomp for main ART composition" );
		createEditingPrecomp.value = true;
		var editingRateRow = importPanel.add( "group" );
		editingRateRow.margins = [ 20, 0, 0, 0 ];
		editingRateRow.add( "statictext", undefined, "Editing precomp FPS:" );
		var editingRate = editingRateRow.add( "edittext", undefined, "25" );
		editingRate.characters = 7;
		var separateObjectId = importPanel.add(
			"checkbox", undefined, "Separate ObjectID into four category matte compositions" );
		separateObjectId.value = true;
		var importCamioCamera = importPanel.add(
			"checkbox", undefined, "Import mirv_camio camera (.cam)" );
		importCamioCamera.value = true;
		var importCamexportCamera = importPanel.add(
			"checkbox", undefined, "Import mirv_camexport camera (.bvh)" );
		importCamexportCamera.value = false;

		var agrPanel = window.add( "panel", undefined, "HLAE AGR import" );
		agrPanel.orientation = "column";
		agrPanel.alignChildren = [ "left", "center" ];
		agrPanel.margins = 10;

		var importAgrPlayers = agrPanel.add(
			"checkbox", undefined, "Player positions only as 3D nulls" );
		importAgrPlayers.value = false;
		var importAgrFull = agrPanel.add(
			"checkbox", undefined, "Full AGR entity and camera nulls" );
		importAgrFull.value = false;

		var agrDetailGroup = agrPanel.add( "group" );
		agrDetailGroup.orientation = "column";
		agrDetailGroup.alignChildren = [ "left", "center" ];
		agrDetailGroup.margins = [ 20, 0, 0, 0 ];
		var importAgrBones = agrDetailGroup.add(
			"checkbox", undefined, "Include indexed bone nulls" );
		importAgrBones.value = true;
		var importAgrAfxCam = agrDetailGroup.add(
			"checkbox", undefined, "Include global afxCam null" );
		importAgrAfxCam.value = true;
		agrDetailGroup.enabled = false;

		// The two modes read the same AGR stream and would duplicate player nulls.
		function updateAgrOptions()
		{
			agrDetailGroup.enabled = importAgrFull.value;
		}
		importAgrPlayers.onClick = function()
		{
			if ( importAgrPlayers.value )
				importAgrFull.value = false;
			updateAgrOptions();
		};
		importAgrFull.onClick = function()
		{
			if ( importAgrFull.value )
				importAgrPlayers.value = false;
			updateAgrOptions();
		};

		var status = window.add( "statictext", undefined,
			"Select a [take].json file generated by ART.", { multiline: true } );
		status.preferredSize.height = 44;

		var buttons = window.add( "group" );
		buttons.alignment = [ "right", "top" ];
		var importButton = buttons.add( "button", undefined, "Import take" );
		var closeButton = buttons.add( "button", undefined, "Close" );

		browse.onClick = function()
		{
			var selected = File.openDialog(
				"Select an ART [take].json manifest", "JSON:*.json" );
			if ( selected )
				manifestPath.text = selected.fsName;
		};
		function updateImportUi()
		{
			videoRow.enabled = mode.selection && mode.selection.index === 1;
			createEditingPrecomp.enabled = createComposition.value;
			editingRateRow.enabled = createComposition.value &&
				createEditingPrecomp.value;
		}
		mode.onChange = updateImportUi;
		createComposition.onClick = updateImportUi;
		createEditingPrecomp.onClick = updateImportUi;
		updateImportUi();
		closeButton.onClick = function()
		{
			if ( window instanceof Window )
				window.close();
		};
		importButton.onClick = function()
		{
			if ( !manifestPath.text )
			{
				alert( "Choose an ART take JSON file first.", SCRIPT_NAME );
				return;
			}
			status.text = "Importing...";
			window.update();
			app.beginUndoGroup( "Import ART take" );
			try
			{
				var extensionText = videoExtension.selection ?
					String( videoExtension.selection.text ).toLowerCase() : "auto";
				if ( extensionText.charAt( 0 ) === "." )
					extensionText = extensionText.substring( 1 );
				var keyValue = "keylight";
				if ( keyMode.selection && keyMode.selection.index === 1 )
					keyValue = "linear";
				else if ( keyMode.selection && keyMode.selection.index === 2 )
					keyValue = "none";
				var selectedMediaMode = "sequence";
				if ( mode.selection && mode.selection.index === 1 )
					selectedMediaMode = "video";
				else if ( mode.selection && mode.selection.index === 2 )
					selectedMediaMode = "exr";
				var result = importTake( {
					manifestPath: manifestPath.text,
					mediaMode: selectedMediaMode,
					videoExtension: extensionText,
					fallbackFrameRate: fallbackRate.text,
					keyMode: keyValue,
					createComposition: createComposition.value,
					createEditingPrecomp: createEditingPrecomp.value,
					editingFrameRate: editingRate.text,
					separateObjectId: separateObjectId.value,
					importCamioCamera: importCamioCamera.value,
					importCamexportCamera: importCamexportCamera.value,
					importAgrPlayers: importAgrPlayers.value,
					importAgrFull: importAgrFull.value,
					importAgrBones: importAgrBones.value,
					importAgrAfxCam: importAgrAfxCam.value
				} );
				var message = result.mediaMode === "exr" ?
					"Imported the multilayer EXR sequence from " +
						result.manifest.take.name + "." :
					"Imported " + result.importedCount + " pass" +
						( result.importedCount === 1 ? "" : "es" ) +
						" from " + result.manifest.take.name + ".";
				if ( result.mediaMode === "exr" && result.exrExpanded )
					message += " Created " + result.extractorLayerCount +
						" configured EXtractoR pass layer" +
						( result.extractorLayerCount === 1 ? "." : "s." );
				if ( result.separatedCount )
					message += " Created " + result.separatedCount + " ObjectID mattes.";
				if ( result.cameraCount )
					message += " Imported " + result.cameraCount + " HLAE camera" +
						( result.cameraCount === 1 ? "." : "s." );
				if ( result.playerNullCount )
					message += " Created " + result.playerNullCount + " AGR player null" +
						( result.playerNullCount === 1 ? "." : "s." );
				if ( result.fullAgrNullCount )
					message += " Created " + result.fullAgrNullCount + " full AGR null" +
						( result.fullAgrNullCount === 1 ? "." : "s." );
				if ( result.editingComp )
					message += " Created editing precomp at " +
						result.editingComp.frameRate + " FPS.";
				if ( result.warnings.length )
				message += "\n\nWarnings:\n- " + result.warnings.join( "\n- " );
				status.text = message;
				if ( result.warnings.length )
					alert( message, SCRIPT_NAME );
			}
			catch ( error )
			{
				status.text = "Import failed: " + error.toString();
				alert( status.text, SCRIPT_NAME );
			}
			finally
			{
				app.endUndoGroup();
			}
		};

		window.onResizing = window.onResize = function()
		{
			this.layout.resize();
		};
		window.layout.layout( true );
		return window;
	}

	var ui = buildUi( thisObject );
	if ( ui instanceof Window )
	{
		ui.center();
		ui.show();
	}
} )( this );
