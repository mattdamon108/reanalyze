/* Adapted from https://github.com/LexiFi/dead_code_analyzer */

open DeadCommon;

let typeDependencies = ref([]);

let addTypeReference = (~posFrom, ~posTo) => {
  if (verbose) {
    Log_.item(
      "addTypeReference %s --> %s@.",
      posFrom |> posToString,
      posTo |> posToString,
    );
  };
  PosHash.addSet(typeReferences, posTo, posFrom);
};

let addDeclaration =
    (~path as path_, {type_kind, type_manifest}: Types.type_declaration) => {
  let save = (~declKind, ~loc: Location.t, ~name) => {
    let isInterfaceFile = Filename.check_suffix(loc.loc_start.pos_fname, "i");
    let name = isInterfaceFile ? name : "+" ++ name;
    let path = [name, ...path_] |> pathToString;
    if (type_manifest == None) {
      addTypeDeclaration(~declKind, ~path=path_, ~loc, name);
    };
    Hashtbl.replace(fields, path, loc);
  };

  switch (type_kind) {
  | Type_record(l, _) =>
    List.iter(
      ({Types.ld_id, ld_loc}) =>
        save(~declKind=RecordLabel, ~loc=ld_loc, ~name=Ident.name(ld_id)),
      l,
    )
  | Type_variant(l) =>
    List.iter(
      ({Types.cd_id, cd_loc}) =>
        save(~declKind=VariantCase, ~loc=cd_loc, ~name=Ident.name(cd_id)),
      l,
    )
  | _ => ()
  };
};

let processTypeDeclaration = (typeDeclaration: Typedtree.type_declaration) => {
  let extendTypeDependencies = (loc1: Location.t, loc2: Location.t) =>
    if (loc1.loc_start != loc2.loc_start) {
      if (verbose) {
        Log_.item(
          "extendTypeDependencies %s --> %s@.",
          loc1.loc_start |> posToString,
          loc2.loc_start |> posToString,
        );
      };

      typeDependencies := [(loc1, loc2), ...typeDependencies^];
    };
  let updateDependencies = (~isVariant, name, loc) => {
    let path2 =
      [
        currentModuleName^,
        ...List.rev([
             name.Asttypes.txt,
             typeDeclaration.typ_name.txt,
             ...currentModulePath^,
           ]),
      ]
      |> String.concat(".");

    try(
      switch (typeDeclaration.typ_manifest) {
      | Some({ctyp_desc: Ttyp_constr(_, {txt}, _)}) =>
        let path1 =
          [currentModuleName^, ...Longident.flatten(txt)]
          @ [name.Asttypes.txt]
          |> String.concat(".");
        let loc1 = Hashtbl.find(fields, path1);
        let loc2 = Hashtbl.find(fields, path2);
        extendTypeDependencies(loc, loc1);
        extendTypeDependencies(loc1, loc2);
      | _ => ()
      }
    ) {
    | _ => ()
    };
    switch (Hashtbl.find_opt(fields, path2)) {
    | Some(loc2) =>
      extendTypeDependencies(loc, loc2);
      if (isVariant && !reportVariantDeadOnlyInInterface) {
        extendTypeDependencies(loc2, loc);
      };
    | None => Hashtbl.add(fields, path2, loc)
    };
  };

  switch (typeDeclaration.typ_kind) {
  | Ttype_record(l) =>
    l
    |> List.iter(({Typedtree.ld_name, ld_loc}) =>
         updateDependencies(~isVariant=false, ld_name, ld_loc)
       )

  | Ttype_variant(l) =>
    l
    |> List.iter(({Typedtree.cd_name, cd_loc}) =>
         updateDependencies(~isVariant=true, cd_name, cd_loc)
       )

  | _ => ()
  };
};