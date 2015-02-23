var en_US = {
    
    common: {
        all: 'All',
        coming_soon: 'Coming soon...',
        password: 'Password',
        confirm_password: 'Confirm Password',
        username: 'Username',
        days: 'days',
        hours: 'hours',
        minutes: 'minutes',
        months: 'months',
        years: 'years',
        l_back: 'Back',
        l_cancel: 'Cancel',
        l_confirm: 'Confirm',
        l_custom: 'Custom',
        l_done: 'Done',
        l_edit: 'Edit',
        l_error: 'Error',
        l_high: 'High',
        l_info: 'Information',
        l_low: 'Low',
        l_never: 'Never',
        l_next: 'Next',
        l_no: 'No',
        l_none: 'None',
        l_ok: 'OK',
        l_on: 'On',
        l_off: 'Off',
        l_required: 'Required',
        l_save: 'Save',
        l_search: 'Search',
        l_warning: 'Warning',
        l_yes: 'Yes',
        th_date: 'Date',
        th_data_type: 'Data Type',
        th_name: 'Name',
        th_application: 'Application',
        th_remove: 'Remove',
        th_tenant: 'Tenant',
        
        //time labels
        l_30_days: '30 Days',
        l_15_days: '15 Days',
        l_1_week: '1 Week',
        l_1_day: '1 Day',
        l_1_hour: '1 Hour',
        l_today: 'Today',
        l_yesterday: 'Yesterday',
        l_30_minutes: '30 Minutes',
        l_15_minutes: '15 Minutes',
        l_now: 'Now',
        
        l_minutes_ago: 'minutes ago',
        l_hours_ago: 'hours ago',
        l_weeks_ago: 'weeks ago',
        l_years_ago: 'years ago',
        l_seconds_ago: 'seconds ago',
        
        l_minute_ago: 'minute ago',
        l_hour_ago: 'hour ago',
        l_week_ago: 'week ago',
        l_second_ago: 'second ago',
        
        l_months: 'Months',
        l_days: 'Days',
        l_hours: 'Hours',
        l_weeks: 'Weeks',
        l_years: 'Years',
        l_minutes: 'Minutes',
        l_seconds: 'Seconds',
        l_millis: 'Milliseconds',
        l_months_by_days: 'Months by days',
        l_forever: 'Forever',
        
        l_month: 'Month',
        l_day: 'Day',
        l_week: 'Week',
        l_year: 'Year',
        l_second: 'Second',
        l_minute: 'Minute',
        l_milli: 'Millisecond',
        
        bytes: 'bytes',
        kb: 'KB',
        mb: 'MB',
        gb: 'GB',
        tb: 'TB',
        pb: 'PB',
        eb: 'EB'
    },
    login: {
        lost_your_password: 'Lost your password?',
        remember_me: 'Remember me',
        submit: 'Login'
    },
    status: {
        activity: 'Activity',
        title: 'Status',
        
        l_capacity: 'Capacity',
        l_firebreak: 'Firebreak',
        l_performance: 'Performance',
        l_health: 'System Health',
        
        l_gets: 'GET requests',
        l_ssd_gets: 'GET request from SSD',
        l_puts: 'PUT requests',
        
        th_type: 'Type',
        th_message: 'Message',
        th_received: 'Received',
        
        tt_firebreak: 'Firebreak {{value}} {{units}} ago.',
        tt_firebreak_never: 'This volume has never had a firebreak.',
        
        desc_firebreak: 'Events in the past 24 hours',
        desc_performance: 'Transactions per Second',
        desc_capacity_used: 'Total capacity used',
        desc_dedup_ratio: 'Deduplication ratio',
        desc_dedup_capacity: 'Deduplicated storage capacity used.',
        desc_pre_dedup_capacity: 'Logical storage capacity consumed before deduplication.',
        desc_time_to_full: 'until capacity is full',
        
        l_services_good: 'All expected services are running properly',
        l_services_not_good: 'Some services are not running as expected',
        l_services_bad: 'Necessary services are not running and require attention',
        
        l_capacity_good: 'System capacity has adequate free space',
        l_capacity_not_good_threshold: 'System capacity is above 80%',
        l_capacity_not_good_rate: 'System capacity is predicated to run out in less than 30 days',
        l_capacity_bad_threshold: 'System capacity is above 90%',
        l_capacity_bad_rate: 'System capacity is predicted to run out in less than 1 week',
        
        l_firebreak_good: 'No firebreak events in the last 24 hours',
        l_firebreak_not_good: 'Some volumes have experienced a firebreak in the last 24 hours',
        l_firebreak_bad: 'Most volumes have experience a firebreak in the last 24 hours',
        
        l_excellent: 'Excellent',
        l_good: 'Good',
        l_acceptable: 'Acceptable',
        l_marginal: 'Marginal',
        l_limited: 'Limited',
        l_unknown: 'Unknown'
    },
    inbox: {
        messages: 'Messages',
        title: 'Inbox'
    },
    activities: {
        title: 'Activities',
        header: 'System Activity',
        l_firebreak: 'Firebreak',
        l_performance: 'Performance',
        l_storage: 'Storage',
        l_system: 'System',
        l_volumes: 'Volumes',
        
        desc_no_activities: 'No activities or events in the system.'
    },
    volumes: {
        title: 'Volumes',
        desc_cloning: 'Settings will be auto-filled below, you may choose to change some of them.',
        desc_confirm_delete: 'Are you sure you want to delete this volume?',
        desc_if_applicable: '(if applicable)',
        desc_no_volumes: 'There are no registered volumes.',
        desc_snapshot_created: 'Snapshot successfully created.',
        desc_confirm_snapshot: 'Are you sure you would like to create a snapshot for this volume?',
        desc_volume_deleted: 'Volume deleted successfully.',
        l_blank: 'Blank',
        l_clone: 'Clone',
        l_clone_existing: 'Clone from an existing volume',
        l_volume_to_clone_from: 'Volume to Clone from:',
        l_continuous: 'Continuous',
        l_create_volume: 'Create Volume',
        l_create_blank_volume: 'Create a blank volume',
        l_current_state: 'Current state',
        l_data_connector: 'Data Connector',
        l_delete_volume: 'Delete this volume.',
        l_edit_settings: 'Edit Settings',
        l_edit_volume: 'Edit Volume',
        l_point_timeline: 'Point from timeline:',
        l_qos: 'Performance',
        l_save_volume: 'Save Volume',
        l_select_to_clone: 'Select a Volume to Clone',
        l_settings: 'Settings',
        l_size_limit: 'Capacity Limit',
        l_snapshots: 'Snapshots',
        l_snapshot_policy: 'Snapshot Policy',
        l_snapshot_policies: 'Snapshot Policies',
        l_starting_point: 'Starting Point',
        l_timeline: 'Timeline',
        l_tiering: 'Tiering',
        l_volume_name: 'Volume Name',
        f_priority: 'Priority:',
        th_capacity: 'Capacity',
        th_capacity_limit: 'Capacity Limit',
        th_expiration: 'Expiration',
        th_priority: 'Priority',
        th_used_limit: 'Used/Limit',
        
        tt_volume_name: 'Volume name',
        tt_tenant_name: 'Tenant name',
        tt_data_connector: 'Volume data type',
        tt_size: 'The capacity consumed for the volume',
        tt_firebreak: 'Firebreak status over the past 24 hours',
        tt_priority: 'Volume priority',
        
        view : {
            desc_dedup_suffix: 'Deduplicated',
            desc_logical_suffix: 'Logical',
            desc_iops_capacity: 'IOPs Capacity',
            l_data_connector_settings: 'Data Connector Settings',
            l_performance_settings: 'Performance Settings',
            l_volume_details: 'Volume Details',
            l_avg_puts: 'Avg. # of PUTS',
            l_avg_gets: 'Avg. # of GETS',
            l_avg_ssd_gets: 'Avg. # of GETS from SSD'
        },
        
        qos: {
            l_highest: 'Highest',
            l_l_highest: '(highest)',
            l_iops: 'IOPs',
            l_iops_guarantee: 'IOPs Capacity Guarantee',
            l_iops_limit: 'IOPs Limit',
            l_lowest: 'Lowest',
            l_l_lowest: '(lowest)',
            l_max_iops: 'Max number of IOPs',
            l_number_of_iops: 'Number of IOPs',
            l_priority: 'Priority',
            l_least_important: 'Least Important',
            l_standard: 'Standard',
            l_most_important: 'Most Important',
            
            f_iops_guarantee: 'IOPs Capacity Guarantee:',
            f_iops_limit: 'IOPs Limit:',
            f_policy: 'Policy:',
            
            th_iops_guarantee: 'IOPs Capacity Guarantee',
            th_iops_limit: 'IOPs Limit',
            th_performance: 'Performance'
        },
        
        tiering: {
            l_ssd_only: 'Flash Only',
            l_disk_only: 'Disk Only',
            l_ssd_preferred: 'Flash Preferred',
            l_disk_preferred: 'Disk Preferred',
            l_hybrid: 'Hybrid'
        },
        
        snapshot: {
            
            l_sparse: 'Sparse Coverage',
            l_standard: 'Standard',
            l_dense: 'Dense Coverage',
            
            l_time_of_retained_snapshots: 'Time of Retained Snapshots',
            l_no_snapshots: 'There are no snapshots for this volume.',
            l_retention_summary: 'Keep for',
            
            l_daily: 'Daily',
            l_weekly: 'Weekly',
            l_monthly: 'Monthly',
            l_yearly: 'Yearly',
            
            l_first_weekly: 'First week of the month',
            l_last_weekly: 'Last week of the month',
            l_first_day_of_the_month: 'First day of the month',
            l_last_day_of_the_month: 'Last day of the month',
            
            l_january: 'January',
            l_february: 'Febraury',
            l_march: 'March',
            l_april: 'April',
            l_may: 'May',
            l_june: 'June',
            l_july: 'July',
            l_august: 'August',
            l_september: 'September',
            l_october: 'October',
            l_november: 'November',
            l_december: 'December',
            
            l_on_monday: 'on Mondays',
            l_on_tuesday: 'on Tuesdays',
            l_on_wednesday: 'on Wednesdays',
            l_on_thursday: 'on Thursdays',
            l_on_friday: 'on Fridays',
            l_on_saturday: 'on Saturdays',
            l_on_sunday: 'on Sundays',
            l_on_1: 'on the 1st day of the month',
            l_on_2: 'on the 2nd day of the month',
            l_on_3: 'on the 3rd day of the month',
            l_on_4: 'on the 4th day of the month',
            l_on_5: 'on the 5th day of the month',
            l_on_6: 'on the 6th day of the month',
            l_on_7: 'on the 7th day of the month',
            l_on_8: 'on the 8th day of the month',
            l_on_9: 'on the 9th day of the month',
            l_on_10: 'on the 10th day of the month',
            l_on_11: 'on the 11th day of the month',
            l_on_12: 'on the 12th day of the month',
            l_on_13: 'on the 13th day of the month',
            l_on_14: 'on the 14th day of the month',
            l_on_15: 'on the 15th day of the month',
            l_on_16: 'on the 16th day of the month',
            l_on_17: 'on the 17th day of the month',
            l_on_18: 'on the 18th day of the month',
            l_on_19: 'on the 19th day of the month',
            l_on_20: 'on the 20th day of the month',
            l_on_21: 'on the 21st day of the month',
            l_on_22: 'on the 22nd day of the month',
            l_on_23: 'on the 23rd day of the month',
            l_on_24: 'on the 24th day of the month',
            l_on_25: 'on the 25th day of the month',
            l_on_26: 'on the 26th day of the month',
            l_on_27: 'on the 27th day of the month',
            l_on_28: 'on the 28th day of the month',
            l_on_l: 'on the last day of the month',
        }
    },
    system: {
        title: 'System',
        l_add_node: 'Add Node',
        l_nodes: 'Nodes',
        th_am: 'AM',
        th_dm: 'DM',
        th_hw: 'HW',
        th_local_domain: 'Local Domain',
        th_node_name: 'Node Name',
        th_om: 'OM',
        th_site: 'Site',
        th_sm: 'SM',
        th_pm: 'PM',
        th_ip_address: 'IP Address',
        
        tt_node_name: 'Name of the node',
        tt_am: 'Access manager status',
        tt_dm: 'Data manager status',
        tt_om: 'Orchestration manager status',
        tt_pm: 'Platform status',
        tt_sm: 'Storage manager status',
        tt_ip_address: 'The IP address for the discovered node',
        tt_remove: 'Button to remove the selected node from the system',
        
        desc_remove_node: 'Are you sure you want to remove this node from the system?',
        desc_node_removed: 'Node was removed successfully.',
        desc_add_partial_warning: 'You have additional nodes available that have not been selected. It is optimal to add all new nodes at one time. Are you sure you want to continue?'
    },
    tenants: {
        title: 'Tenants',
        l_create_tenant: 'Create a Tenant',
        l_create_tenant_title: 'Create Tenant',
        l_tenant_name: 'Tenant Name'
    },
    users: {
        title: 'Users',
        l_create_user: 'Create User',
        l_edit_user: 'Edit User',
        
        th_tenant: 'Tenant'
    },
    admin: {
        title: 'Admin',
        header: 'Administration',
        l_data_connectors: 'Data Connectors',
        l_log_alert: 'Log and Alert Settings',
        l_version: 'System Version',
        l_release_notes: 'Release notes',
        l_rollback: 'Rollback',
        l_update: 'Update Now',
        l_update_available: 'Update available:',
        th_api: 'API',
        th_data_type: 'Data Type',
        th_default_capacity: 'Default Capacity'
    },
    account: {
        title: 'Account Details',
        desc_tenant_admin: 'Tenant Admins can create modify and delete Tenant and App Developer users.  Additionally you can modify most system settings,create storage volumes and do everything else lower-level users can do.',
        l_change_email: 'Change Email',
        l_change_password: 'Change Password',
        l_change_role: 'Change Role',
        l_email: 'Email',
        l_name: 'Name',
        l_role: 'Role',
        l_tenant_admin: 'Tenant Admin'
    }
};
