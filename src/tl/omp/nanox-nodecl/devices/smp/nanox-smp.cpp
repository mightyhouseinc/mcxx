/*--------------------------------------------------------------------
  (C) Copyright 2006-2012 Barcelona Supercomputing Center
                          Centro Nacional de Supercomputacion
  
  This file is part of Mercurium C/C++ source-to-source compiler.
  
  See AUTHORS file in the top level directory for information 
  regarding developers and contributors.
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.
  
  Mercurium C/C++ source-to-source compiler is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU Lesser General Public License for more
  details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with Mercurium C/C++ source-to-source compiler; if
  not, write to the Free Software Foundation, Inc., 675 Mass Ave,
  Cambridge, MA 02139, USA.
--------------------------------------------------------------------*/

#include "tl-devices.hpp"
#include "nanox-smp.hpp"

#include "tl-lowering-visitor.hpp"
#include "tl-source.hpp"
#include "tl-counters.hpp"
#include "tl-nodecl-utils.hpp"
#include "tl-outline-info.hpp"
#include "tl-replace.hpp"
#include "tl-compilerpipeline.hpp"

#include "tl-nodecl-utils-fortran.hpp"

#include "codegen-phase.hpp"
#include "codegen-fortran.hpp"

#include "cxx-cexpr.h"
#include "fortran03-scope.h"
#include "fortran03-typeutils.h"
#include "fortran03-buildscope.h"

#include "cxx-profile.h"
#include "cxx-driver-utils.h"
#include "cxx-symbol-deep-copy.h"

#include <errno.h>
#include <string.h>

using TL::Source;

namespace TL { namespace Nanox {

    static std::string smp_outline_name(const std::string &task_name)
    {
        return "smp_" + task_name;
    }

    static TL::Symbol new_function_symbol(
            TL::Symbol current_function,
            const std::string& name,
            TL::Type return_type,
            ObjectList<std::string> parameter_names,
            ObjectList<TL::Type> parameter_types)
    {
        decl_context_t decl_context = current_function.get_scope().get_decl_context();
        ERROR_CONDITION(parameter_names.size() != parameter_types.size(), "Mismatch between names and types", 0);

        decl_context_t function_context;
        if (IS_FORTRAN_LANGUAGE)
        {
            function_context = new_program_unit_context(decl_context);
        }
        else
        {
            function_context = new_function_context(decl_context);
            function_context = new_block_context(function_context);
        }

        // Build the function type
        int num_parameters = 0;
        scope_entry_t** parameter_list = NULL;

        parameter_info_t* p_types = new parameter_info_t[parameter_types.size()];
        parameter_info_t* it_ptypes = &(p_types[0]);
        ObjectList<TL::Type>::iterator type_it = parameter_types.begin();
        for (ObjectList<std::string>::iterator it = parameter_names.begin();
                it != parameter_names.end();
                it++, it_ptypes++, type_it++)
        {
            scope_entry_t* param = new_symbol(function_context, function_context.current_scope, it->c_str());
            param->entity_specs.is_user_declared = 1;
            param->kind = SK_VARIABLE;
            param->file = "";
            param->line = 0;

            param->defined = 1;

            param->type_information = get_unqualified_type(type_it->get_internal_type());

            P_LIST_ADD(parameter_list, num_parameters, param);

            it_ptypes->is_ellipsis = 0;
            it_ptypes->nonadjusted_type_info = NULL;
            it_ptypes->type_info = get_indirect_type(param);
        }

        type_t *function_type = get_new_function_type(
                return_type.get_internal_type(),
                p_types,
                parameter_types.size());

        delete[] p_types;

        // Now, we can create the new function symbol
        scope_entry_t* new_function_sym = NULL;
        if (!current_function.get_type().is_template_specialized_type())
        {
            new_function_sym = new_symbol(decl_context, decl_context.current_scope, name.c_str());
            new_function_sym->entity_specs.is_user_declared = 1;
            new_function_sym->kind = SK_FUNCTION;
            new_function_sym->file = "";
            new_function_sym->line = 0;
            new_function_sym->type_information = function_type;
        }
        else
        {
            scope_entry_t* new_template_sym = new_symbol(
                    decl_context, decl_context.current_scope, name.c_str());
            new_template_sym->kind = SK_TEMPLATE;
            new_template_sym->file = "";
            new_template_sym->line = 0;

            new_template_sym->type_information = get_new_template_type(
                    decl_context.template_parameters,
                    function_type,
                    uniquestr(name.c_str()),
                    decl_context, 0, "");

            template_type_set_related_symbol(new_template_sym->type_information, new_template_sym);

            // The new function is the primary template specialization
            new_function_sym = named_type_get_symbol(
                    template_type_get_primary_type(
                        new_template_sym->type_information));
        }

        function_context.function_scope->related_entry = new_function_sym;
        function_context.block_scope->related_entry = new_function_sym;

        new_function_sym->related_decl_context = function_context;

        new_function_sym->entity_specs.related_symbols = parameter_list;
        new_function_sym->entity_specs.num_related_symbols = num_parameters;
        for (int i = 0; i < new_function_sym->entity_specs.num_related_symbols; ++i)
        {
            symbol_set_as_parameter_of_function(
                    new_function_sym->entity_specs.related_symbols[i], new_function_sym, /* parameter position */ i);
        }

        // Make it static
        new_function_sym->entity_specs.is_static = 1;

        // Make it member if the enclosing function is member
        if (current_function.is_member())
        {
            new_function_sym->entity_specs.is_member = 1;
            new_function_sym->entity_specs.class_type = current_function.get_class_type().get_internal_type();

            new_function_sym->entity_specs.access = AS_PUBLIC;

            ::class_type_add_member(new_function_sym->entity_specs.class_type, new_function_sym);
        }
        return new_function_sym;
    }

    static void build_empty_body_for_function(
            TL::Symbol function_symbol,
            Nodecl::NodeclBase &function_code,
            Nodecl::NodeclBase &empty_stmt
            )
    {
        empty_stmt = Nodecl::EmptyStatement::make("", 0);
        Nodecl::List stmt_list = Nodecl::List::make(empty_stmt);

        if (IS_C_LANGUAGE || IS_CXX_LANGUAGE)
        {
            Nodecl::CompoundStatement compound_statement =
                Nodecl::CompoundStatement::make(stmt_list,
                        /* destructors */ Nodecl::NodeclBase::null(),
                        "", 0);
            stmt_list = Nodecl::List::make(compound_statement);
        }

        Nodecl::NodeclBase context = Nodecl::Context::make(
                stmt_list,
                function_symbol.get_related_scope(), "", 0);

        function_symbol.get_internal_symbol()->defined = 1;

        if (function_symbol.is_dependent_function())
        {
            function_code = Nodecl::TemplateFunctionCode::make(context,
                    // Initializers
                    Nodecl::NodeclBase::null(),
                    // Internal functions
                    Nodecl::NodeclBase::null(),
                    function_symbol,
                    "", 0);
        }
        else
        {
            function_code = Nodecl::FunctionCode::make(context,
                    // Initializers
                    Nodecl::NodeclBase::null(),
                    // Internal functions
                    Nodecl::NodeclBase::null(),
                    function_symbol,
                    "", 0);
        }
    }

    // Rewrite inline
    struct RewriteExprOfVla : public Nodecl::ExhaustiveVisitor<void>
    {
        private:
            const TL::ObjectList<OutlineDataItem*> &_data_items;
            TL::Symbol &_args_symbol;

        public:

        RewriteExprOfVla(const TL::ObjectList<OutlineDataItem*> &data_items, TL::Symbol &args_symbol)
            : _data_items(data_items),
            _args_symbol(args_symbol)
        { }

        virtual void visit(const Nodecl::Symbol& node)
        {
            TL::Symbol sym = node.get_symbol();
            for (TL::ObjectList<OutlineDataItem*>::const_iterator it = _data_items.begin();
                    it != _data_items.end();
                    it++)
            {
                if (sym == (*it)->get_symbol())
                {
                    Nodecl::NodeclBase new_class_member_access;
                    // x -> args.x
                    Nodecl::NodeclBase new_args_ref = Nodecl::Symbol::make(_args_symbol);
                    // Should be a reference already
                    new_args_ref.set_type(_args_symbol.get_type());

                    Nodecl::NodeclBase field_ref = Nodecl::Symbol::make((*it)->get_field_symbol());
                    field_ref.set_type(field_ref.get_symbol().get_type());

                    new_class_member_access = Nodecl::ClassMemberAccess::make(
                            new_args_ref,
                            field_ref,
                            // The type of this node should be the same
                            node.get_type());

                    node.replace(new_class_member_access);
                    break;
                }
            }
        }
    };

    TL::Type DeviceSMP::rewrite_type_of_vla_in_outline(
            TL::Type t,
            const TL::ObjectList<OutlineDataItem*> &data_items,
            TL::Symbol &arguments_symbol)
    {
        if (t.is_pointer())
        {
            TL::Type p = rewrite_type_of_vla_in_outline(
                    t.points_to(),
                    data_items,
                    arguments_symbol);

            return p.get_pointer_to();
        }
        else if (t.is_lvalue_reference())
        {
            TL::Type item = rewrite_type_of_vla_in_outline(
                    t.references_to(),
                    data_items,
                    arguments_symbol);

            return item.get_lvalue_reference_to();
        }
        else if (t.is_array())
        {
            TL::Type elem = rewrite_type_of_vla_in_outline(
                    t.array_element(),
                    data_items,
                    arguments_symbol);

            Nodecl::NodeclBase new_size = t.array_get_size().shallow_copy();
            RewriteExprOfVla rewrite_expr_of_vla(data_items, arguments_symbol);
            rewrite_expr_of_vla.walk(new_size);

            return elem.get_array_to(new_size, new_size.retrieve_context());
        }
        // Do nothing
        else return t;
    }

    void DeviceSMP::create_outline(CreateOutlineInfo& info,
            Nodecl::NodeclBase& outline_placeholder,
            Nodecl::NodeclBase& output_statements,
            Nodecl::Utils::SymbolMap* &symbol_map)
    {
        // Unpack DTO
        const std::string& outline_name = smp_outline_name(info._outline_name);
        const Nodecl::NodeclBase& original_statements = info._original_statements;
        bool is_function_task = info._called_task.is_valid();

        output_statements = original_statements;

        TL::Symbol current_function =
            original_statements.retrieve_context().get_decl_context().current_scope->related_entry;
        if (current_function.is_nested_function())
        {
            if (IS_C_LANGUAGE || IS_CXX_LANGUAGE)
                running_error("%s: error: nested functions are not supported\n",
                        original_statements.get_locus().c_str());
            if (IS_FORTRAN_LANGUAGE)
                running_error("%s: error: internal subprograms are not supported\n",
                        original_statements.get_locus().c_str());
        }

        Source extra_declarations;
        Source final_statements, initial_statements;

        // *** Unpacked (and forward in Fortran) function ***
        TL::Symbol unpacked_function, forward_function;
        if (IS_FORTRAN_LANGUAGE)
        {
            forward_function = new_function_symbol_forward(
                    current_function,
                    outline_name + "_forward",
                    info);
            unpacked_function = new_function_symbol_unpacked(
                    current_function,
                    outline_name + "_unpack",
                    info,
                    // out
                    symbol_map,
                    initial_statements,
                    final_statements);
        }
        else
        {
            unpacked_function = new_function_symbol_unpacked(
                    current_function,
                    outline_name + "_unpacked",
                    info,
                    // out
                    symbol_map,
                    initial_statements,
                    final_statements);
        }

        Nodecl::NodeclBase unpacked_function_code, unpacked_function_body;
        build_empty_body_for_function(unpacked_function,
                unpacked_function_code,
                unpacked_function_body);

        if (IS_FORTRAN_LANGUAGE)
        {
            // Now get all the needed internal functions and replicate them in the outline
            Nodecl::Utils::Fortran::InternalFunctions internal_functions;
            internal_functions.walk(info._original_statements);

            Nodecl::List l;
            for (TL::ObjectList<Nodecl::NodeclBase>::iterator
                    it2 = internal_functions.function_codes.begin();
                    it2 != internal_functions.function_codes.end();
                    it2++)
            {
                l.append(
                        Nodecl::Utils::deep_copy(*it2, unpacked_function.get_related_scope(), *symbol_map)
                        );
            }

            unpacked_function_code.as<Nodecl::FunctionCode>().set_internal_functions(l);
        }

        Nodecl::Utils::append_to_top_level_nodecl(unpacked_function_code);

        Source unpacked_source;
        if (!IS_FORTRAN_LANGUAGE)
        {
            unpacked_source
                << "{";
        }
        unpacked_source
            << extra_declarations
            << initial_statements
            << statement_placeholder(outline_placeholder)
            << final_statements
            ;
        if (!IS_FORTRAN_LANGUAGE)
        {
            unpacked_source
                << "}";
        }

        // Fortran may require more symbols
        if (IS_FORTRAN_LANGUAGE)
        {
            // Insert extra symbols
            TL::Scope unpacked_function_scope = unpacked_function_body.retrieve_context();

            Nodecl::Utils::Fortran::ExtraDeclsVisitor fun_visitor(symbol_map, unpacked_function_scope);
            fun_visitor.insert_extra_symbols(original_statements);

            Nodecl::Utils::Fortran::copy_used_modules(
                    original_statements.retrieve_context(),
                    unpacked_function_scope);

            extra_declarations
                << "IMPLICIT NONE\n";

        }
        else if (IS_CXX_LANGUAGE)
        {
            if (!unpacked_function.is_member())
            {
                Nodecl::NodeclBase nodecl_decl = Nodecl::CxxDecl::make(
                        /* optative context */ nodecl_null(),
                        unpacked_function,
                        original_statements.get_filename(),
                        original_statements.get_line());
                Nodecl::Utils::prepend_to_enclosing_top_level_location(original_statements, nodecl_decl);
            }
        }

        Nodecl::NodeclBase new_unpacked_body = unpacked_source.parse_statement(unpacked_function_body);
        unpacked_function_body.replace(new_unpacked_body);

        // **** Outline function *****
        ObjectList<std::string> structure_name;
        structure_name.append("args");
        ObjectList<TL::Type> structure_type;
        structure_type.append(
                TL::Type(get_user_defined_type(info._arguments_struct.get_internal_symbol())).get_lvalue_reference_to()
                );

        TL::Symbol outline_function = new_function_symbol(
                current_function,
                outline_name,
                TL::Type::get_void_type(),
                structure_name,
                structure_type);

        if (IS_FORTRAN_LANGUAGE
                && current_function.is_in_module())
        {
            scope_entry_t* module_sym = current_function.in_module().get_internal_symbol();

            unpacked_function.get_internal_symbol()->entity_specs.in_module = module_sym;
            P_LIST_ADD(
                    module_sym->entity_specs.related_symbols,
                    module_sym->entity_specs.num_related_symbols,
                    unpacked_function.get_internal_symbol());

            unpacked_function.get_internal_symbol()->entity_specs.is_module_procedure = 1;

            outline_function.get_internal_symbol()->entity_specs.in_module = module_sym;
            P_LIST_ADD(
                    module_sym->entity_specs.related_symbols,
                    module_sym->entity_specs.num_related_symbols,
                    outline_function.get_internal_symbol());
            outline_function.get_internal_symbol()->entity_specs.is_module_procedure = 1;
        }

        Nodecl::NodeclBase outline_function_code, outline_function_body;
        build_empty_body_for_function(outline_function,
                outline_function_code,
                outline_function_body);
        Nodecl::Utils::append_to_top_level_nodecl(outline_function_code);

        // Prepare arguments for the call to the unpack (or forward in Fortran)
        TL::Scope outline_function_scope(outline_function_body.retrieve_context());
        TL::Symbol structure_symbol = outline_function_scope.get_symbol_from_name("args");
        ERROR_CONDITION(!structure_symbol.is_valid(), "Argument of outline function not found", 0);

        Source unpacked_arguments, cleanup_code;
        TL::ObjectList<OutlineDataItem*> data_items = info._data_items;
        TL::ObjectList<OutlineDataItem*>::iterator it = data_items.begin();
        if (IS_CXX_LANGUAGE
                && !is_function_task
                && current_function.is_member()
                && !current_function.is_static()
                && it != data_items.end())
        {
            ++it;
        }

        for (; it != data_items.end(); it++)
        {
            switch ((*it)->get_sharing())
            {
                case OutlineDataItem::SHARING_PRIVATE:
                    {
                        // Do nothing
                        break;
                    }
                case OutlineDataItem::SHARING_SHARED:
                case OutlineDataItem::SHARING_CAPTURE:
                case OutlineDataItem::SHARING_CAPTURE_ADDRESS:
                    {
                        TL::Type param_type = (*it)->get_in_outline_type();

                        Source argument;
                        if (IS_C_LANGUAGE || IS_CXX_LANGUAGE)
                        {
                            // Normal shared items are passed by reference from a pointer,
                            // derreference here
                            if ((*it)->get_sharing() == OutlineDataItem::SHARING_SHARED
                                    && !(IS_CXX_LANGUAGE && (*it)->get_symbol().get_name() == "this"))
                            {
                                if (!param_type.no_ref().depends_on_nonconstant_values())
                                {
                                    argument << "*(args." << (*it)->get_field_name() << ")";
                                }
                                else
                                {
                                    TL::Type ptr_type = (*it)->get_in_outline_type().references_to().get_pointer_to();
                                    TL::Type cast_type = rewrite_type_of_vla_in_outline(ptr_type, data_items, structure_symbol);

                                    argument << "*((" << as_type(cast_type) << ")args." << (*it)->get_field_name() << ")";
                                }
                            }
                            // Any other parameter is bound to the storage of the struct
                            else
                            {
                                if (!param_type.no_ref().depends_on_nonconstant_values())
                                {
                                    argument << "args." << (*it)->get_field_name();
                                }
                                else
                                {
                                    TL::Type cast_type = rewrite_type_of_vla_in_outline(param_type, data_items, structure_symbol);
                                    argument << "(" << as_type(cast_type) << ")args." << (*it)->get_field_name();
                                }
                            }

                            if (IS_CXX_LANGUAGE
                                    && (*it)->get_allocation_policy() == OutlineDataItem::ALLOCATION_POLICY_TASK_MUST_DESTROY)
                            {
                                internal_error("Not yet implemented: call the destructor", 0);
                            }
                        }
                        else if (IS_FORTRAN_LANGUAGE)
                        {
                            argument << "args % " << (*it)->get_field_name();

                            bool is_allocatable = (*it)->get_allocation_policy() & OutlineDataItem::ALLOCATION_POLICY_TASK_MUST_DEALLOCATE_ALLOCATABLE;
                            bool is_pointer = (*it)->get_allocation_policy() & OutlineDataItem::ALLOCATION_POLICY_TASK_MUST_DEALLOCATE_POINTER;

                            if (is_allocatable
                                    || is_pointer)
                            {
                                cleanup_code
                                    << "DEALLOCATE(args % " << (*it)->get_field_name() << ")\n"
                                    ;
                            }
                        }
                        else
                        {
                            internal_error("running error", 0);
                        }

                        unpacked_arguments.append_with_separator(argument, ", ");
                        break;
                    }
                case OutlineDataItem::SHARING_REDUCTION:
                    {
                        // // Pass the original reduced variable as if it were a shared
                        Source argument;
                        if (IS_C_LANGUAGE || IS_CXX_LANGUAGE)
                        {
                            argument << "*(args." << (*it)->get_field_name() << ")";
                        }
                        else if (IS_FORTRAN_LANGUAGE)
                        {
                            argument << "args % " << (*it)->get_field_name();
                        }
                        unpacked_arguments.append_with_separator(argument, ", ");
                        break;
                    }
                default:
                    {
                        internal_error("Unexpected data sharing kind", 0);
                    }
            }
        }

        Source outline_src,
               instrument_before,
               instrument_after;

        if (IS_C_LANGUAGE || IS_CXX_LANGUAGE)
        {
           Source unpacked_function_call;
            if (IS_CXX_LANGUAGE
                    && !is_function_task
                    && current_function.is_member()
                    && !current_function.is_static())
            {
                unpacked_function_call << "args.this_->";
            }

           unpacked_function_call
               << unpacked_function.get_qualified_name() << "(" << unpacked_arguments << ");";

            outline_src
                << "{"
                <<      instrument_before
                <<      unpacked_function_call
                <<      instrument_after
                <<      cleanup_code
                << "}"
                ;

            if (IS_CXX_LANGUAGE)
            {
                if (!outline_function.is_member())
                {
                    Nodecl::NodeclBase nodecl_decl = Nodecl::CxxDecl::make(
                            /* optative context */ nodecl_null(),
                            outline_function,
                            original_statements.get_filename(),
                            original_statements.get_line());
                    Nodecl::Utils::prepend_to_enclosing_top_level_location(original_statements, nodecl_decl);
                }
            }
        }
        else if (IS_FORTRAN_LANGUAGE)
        {
            Source outline_function_addr;

            outline_src
                << instrument_before << "\n"
                << "CALL " << outline_name << "_forward(" << outline_function_addr << unpacked_arguments << ")\n"
                << instrument_after << "\n"
                << cleanup_code
                ;

            outline_function_addr << "LOC(" << unpacked_function.get_name() << ")";
            if (!unpacked_arguments.empty())
            {
                outline_function_addr << ", ";
            }

            // Copy USEd information to the outline and forward functions
            TL::Symbol *functions[] = { &outline_function, &forward_function, NULL };

            for (int i = 0; functions[i] != NULL; i++)
            {
                TL::Symbol &function(*functions[i]);

                Nodecl::Utils::Fortran::copy_used_modules(original_statements.retrieve_context(),
                        function.get_related_scope());
            }

            // Generate ancillary code in C
            add_forward_code_to_extra_c_code(outline_name, data_items, outline_placeholder);
        }
        else
        {
            internal_error("Code unreachable", 0);
        }

        if (instrumentation_enabled())
        {
            get_instrumentation_code(
                    info._called_task,
                    outline_function,
                    outline_function_body,
                    info._task_label,
                    original_statements.get_filename(),
                    original_statements.get_line(),
                    instrument_before,
                    instrument_after);
        }

        Nodecl::NodeclBase new_outline_body = outline_src.parse_statement(outline_function_body);
        outline_function_body.replace(new_outline_body);
    }

    void DeviceSMP::add_forward_code_to_extra_c_code(
            const std::string& outline_name,
            TL::ObjectList<OutlineDataItem*> data_items,
            Nodecl::NodeclBase parse_context)
    {
        Source ancillary_source, parameters;

        ancillary_source
            << "extern void " << outline_name << "_forward_" << "(";
        int num_data_items = data_items.size();
        if (num_data_items == 0)
        {
            ancillary_source << "void (*outline_fun)(void)";
        }
        else
        {
            ancillary_source << "void (*outline_fun)(";
            if (num_data_items == 0)
            {
                ancillary_source << "void";
            }
            else
            {
                for (int i = 0; i < num_data_items; i++)
                {
                    if (i > 0)
                    {
                        ancillary_source << ", ";
                    }
                    ancillary_source << "void *p" << i;
                }
            }
            ancillary_source << ")";

            for (int i = 0; i < num_data_items; i++)
            {
                ancillary_source << ", void *p" << i;
            }
        }
        ancillary_source << ")\n{\n"
            // << "    extern int nanos_free(void*);\n"
            << "    extern int nanos_handle_error(int);\n\n"
            << "    outline_fun(";
        for (int i = 0; i < num_data_items; i++)
        {
            if (i > 0)
            {
                ancillary_source << ", ";
            }
            ancillary_source << "p" << i;
        }
        ancillary_source << ");\n";

        // Free all the allocated descriptors
        // bool first = true;
        // int i = 0;
        // for (TL::ObjectList<OutlineDataItem*>::iterator it = data_items.begin();
        //         it != data_items.end();
        //         it++, i++)
        // {
        //     OutlineDataItem &item (*(*it));

        //     if (item.get_symbol().is_valid()
        //             && item.get_sharing() == OutlineDataItem::SHARING_SHARED)
        //     {
        //         TL::Type t = item.get_symbol().get_type();

        //         if (!item.get_symbol().is_allocatable()
        //                 && t.is_lvalue_reference()
        //                 && t.references_to().is_array()
        //                 && t.references_to().array_requires_descriptor())
        //         {
        //             if (first)
        //             {
        //                 ancillary_source << "   nanos_err_t err;\n";
        //                 first = false;
        //             }

        //             ancillary_source
        //                 << "    err = nanos_free(p" << i << ");\n"
        //                 << "    if (err != NANOS_OK) nanos_handle_error(err);\n"
        //                 ;
        //         }
        //     }
        // }

        ancillary_source << "}\n\n";

        // Parse in C
        Source::source_language = SourceLanguage::C;

        Nodecl::List n = ancillary_source.parse_global(parse_context).as<Nodecl::List>();

        // Restore original source language (Fortran)
        Source::source_language = SourceLanguage::Current;

        _extra_c_code.append(n);
    }

    DeviceSMP::DeviceSMP()
        : DeviceProvider(/* device_name */ std::string("smp"))
    {
        set_phase_name("Nanox SMP support");
        set_phase_description("This phase is used by Nanox phases to implement SMP device support");
    }

    void DeviceSMP::pre_run(DTO& dto)
    {
    }

    void DeviceSMP::run(DTO& dto)
    {
        DeviceProvider::run(dto);
    }

    void DeviceSMP::get_device_descriptor(DeviceDescriptorInfo& info,
            Source &ancillary_device_description,
            Source &device_descriptor,
            Source &fortran_dynamic_init)
    {
        const std::string& outline_name = smp_outline_name(info._outline_name);
        const std::string& arguments_struct = info._arguments_struct;
        TL::Symbol current_function = info._current_function;

        //FIXME: This is confusing. In a future, we should get the template
        //arguments of the outline function and print them

        //Save the original name of the current function
        std::string original_name = current_function.get_name();

        current_function.set_name(outline_name);
        Nodecl::NodeclBase code = current_function.get_function_code();

        Nodecl::Context context = (code.is<Nodecl::TemplateFunctionCode>())
            ? code.as<Nodecl::TemplateFunctionCode>().get_statements().as<Nodecl::Context>()
            : code.as<Nodecl::FunctionCode>().get_statements().as<Nodecl::Context>();

        TL::Scope function_scope = context.retrieve_context();
        std::string qualified_name = current_function.get_qualified_name(function_scope);

        // Restore the original name of the current function
        current_function.set_name(original_name);

        if (!IS_FORTRAN_LANGUAGE)
        {
            // Extra cast for solving some issues of GCC 4.6.* and lowers (this
            // issues seem to be fixed in GCC 4.7 =D)
            std::string ref = IS_CXX_LANGUAGE ? "&" : "*";
            std::string extra_cast = "(void(*)(" + arguments_struct + ref + "))";

            ancillary_device_description
                << "static nanos_smp_args_t " << outline_name << "_args = {"
                << ".outline = (void(*)(void*)) " << extra_cast << " &" << qualified_name
                << "};"
                ;
            device_descriptor
                << "{"
                << /* factory */ "&nanos_smp_factory, &" << outline_name << "_args"
                << "}"
                ;
        }
        else
        {
            ancillary_device_description
                << "static nanos_smp_args_t " << outline_name << "_args;"
                ;

            device_descriptor
                << "{"
                // factory, arg
                << "0, 0"
                << "}"
                ;

            fortran_dynamic_init
                << outline_name << "_args.outline = (void(*)(void*))&" << outline_name << ";"
                << "nanos_wd_const_data.devices[" << info._fortran_device_index << "].factory = &nanos_smp_factory;"
                << "nanos_wd_const_data.devices[" << info._fortran_device_index << "].arg = &" << outline_name << "_args;"
                ;
        }
    }

    bool DeviceSMP::remove_function_task_from_original_source() const
    {
        return false;
    }

    void DeviceSMP::copy_stuff_to_device_file(
            const TL::ObjectList<Nodecl::NodeclBase>& stuff_to_be_copied)
    {
        // This function is expressly empty
    }

    void DeviceSMP::phase_cleanup(DTO& data_flow)
    {
        if (_extra_c_code.is_null())
            return;

        std::string original_filename = TL::CompilationProcess::get_current_file().get_filename();
        std::string new_filename = "smp_aux_nanox_outline_file_" + original_filename  + ".c";

        FILE* ancillary_file = fopen(new_filename.c_str(), "w");
        if (ancillary_file == NULL)
        {
            running_error("%s: error: cannot open file '%s'. %s\n",
                    original_filename.c_str(),
                    new_filename.c_str(),
                    strerror(errno));
        }

        CURRENT_CONFIGURATION->source_language = SOURCE_LANGUAGE_C;

        compilation_configuration_t* configuration = ::get_compilation_configuration("auxcc");
        ERROR_CONDITION (configuration == NULL, "auxcc profile is mandatory when using Fortran", 0);

        // Make sure phases are loaded (this is needed for codegen)
        load_compiler_phases(configuration);

        TL::CompilationProcess::add_file(new_filename, "auxcc");

        ::mark_file_for_cleanup(new_filename.c_str());

        Codegen::CodegenPhase* phase = reinterpret_cast<Codegen::CodegenPhase*>(configuration->codegen_phase);
        phase->codegen_top_level(_extra_c_code, ancillary_file);

        CURRENT_CONFIGURATION->source_language = SOURCE_LANGUAGE_FORTRAN;

        fclose(ancillary_file);
        // Do not forget the clear the code for next files
        _extra_c_code.get_internal_nodecl() = nodecl_null();
    }

} }

EXPORT_PHASE(TL::Nanox::DeviceSMP);
